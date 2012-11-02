/*  arch/arm/mach-lpc313x/time.c
 *
 *  Author:	Durgesh Pattamatta
 *  Copyright (C) 2009 NXP semiconductors
 *
 *  Timer driver for LPC313x & LPC315x.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/time.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>

#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/leds.h>

#include <asm/mach/time.h>
#include <mach/gpio.h>
#include <mach/board.h>


static int lpc313x_clkevt_next_event(unsigned long delta,
    struct clock_event_device *dev)
{
	TIMER_CONTROL(TIMER1_PHYS) = 0;
	TIMER_LOAD(TIMER1_PHYS) = delta;
	TIMER_CONTROL(TIMER1_PHYS) = (TM_CTRL_ENABLE | TM_CTRL_PERIODIC);

	return 0;
}

static void lpc313x_clkevt_mode(enum clock_event_mode mode,
    struct clock_event_device *dev)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		TIMER_CONTROL(TIMER1_PHYS) = 0;
		TIMER_LOAD(TIMER1_PHYS) = CLOCK_TICK_RATE / HZ;
		TIMER_CONTROL(TIMER1_PHYS) = (TM_CTRL_ENABLE | TM_CTRL_PERIODIC);
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		TIMER_CONTROL(TIMER1_PHYS) &= ~TM_CTRL_ENABLE;
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		/*
		 * Disable the timer. When using oneshot, we must also
		 * disable the timer to wait for the first call to
		 * set_next_event().
		 */
		TIMER_CONTROL(TIMER1_PHYS) = 0;
		break;

	case CLOCK_EVT_MODE_RESUME:
		TIMER_CONTROL(TIMER1_PHYS) |= TM_CTRL_ENABLE;
		break;
	}
}
static struct clock_event_device lpc313x_clkevt = {
	.name		= "lpc313x event timer",
        .features       = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC,
        .shift          = 32,
	.rating		= 300,	
        .set_next_event = lpc313x_clkevt_next_event,
        .set_mode       = lpc313x_clkevt_mode,
};

static irqreturn_t lpc313x_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &lpc313x_clkevt;

	/* clear timer interrupt */
	TIMER_CLEAR(TIMER1_PHYS) = 0;

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction lpc313x_timer_irq = {
	.name		= "lpc313x_timer_irq",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= lpc313x_timer_interrupt,
};


static cycle_t lpc313x_clksrc_read(struct clocksource *cs)
{
	BUG_ON((TIMER_CONTROL(TIMER0_PHYS) & TM_CTRL_ENABLE) == 0);
	return ~TIMER_VALUE(TIMER0_PHYS);
}

static struct clocksource lpc313x_clksrc = {
	.name		= "lpc313x_clksrc",
	.rating		= 300,
	.read		= lpc313x_clksrc_read,
	.shift		= 20,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

/*
 * The clock management driver isn't initialized at this point, so the
 * clocks need to be enabled here manually and then tagged as used in
 * the clock driver initialization
 */
static void __init lpc313x_timer_init(void)
{
	/* configure timers to select no fractional divider, but
	 * direct AHB_APB1_BASE clock */
	CGU_SB->base_fdc[CGU_SB_BASE2_FDIV_LOW_ID] = 0;
	CGU_SB->clk_esr[CGU_SB_TIMER0_PCLK_ID] = 0;
	CGU_SB->clk_esr[CGU_SB_TIMER1_PCLK_ID] = 0;
	CGU_SB->clk_esr[CGU_SB_TIMER2_PCLK_ID] = 0;
	CGU_SB->clk_esr[CGU_SB_TIMER3_PCLK_ID] = 0;
	/* Enable timer clock */
	/* Attention: cgu_init() will later go through all clocks and 
	 * enable/disable based on its own information! */
	cgu_clk_en_dis(CGU_SB_TIMER0_PCLK_ID, 1);
	cgu_clk_en_dis(CGU_SB_TIMER1_PCLK_ID, 1);
	cgu_clk_en_dis(CGU_SB_TIMER2_PCLK_ID, 0);
	cgu_clk_en_dis(CGU_SB_TIMER3_PCLK_ID, 0);


	/* set up free running timer 0 */
	TIMER_CONTROL(TIMER0_PHYS) = 0;
	TIMER_LOAD(TIMER0_PHYS) = 0;
	TIMER_CLEAR(TIMER0_PHYS) = 0;
	TIMER_CONTROL(TIMER0_PHYS) = TM_CTRL_ENABLE;


	/* set up event timer 1 */
	TIMER_CONTROL(TIMER1_PHYS) = 0;
	TIMER_LOAD(TIMER1_PHYS) = 0;
	TIMER_CLEAR(TIMER1_PHYS) = 0;
	TIMER_CONTROL(TIMER1_PHYS) = TM_CTRL_PERIODIC;
	setup_irq (IRQ_TIMER1, &lpc313x_timer_irq);
	

	/* Setup the clockevent structure. */
	lpc313x_clkevt.mult = div_sc(CLOCK_TICK_RATE, NSEC_PER_SEC,
		lpc313x_clkevt.shift);
	lpc313x_clkevt.max_delta_ns = clockevent_delta2ns(-1,
		&lpc313x_clkevt);
	lpc313x_clkevt.min_delta_ns = clockevent_delta2ns(1,
		&lpc313x_clkevt) + 1;
	lpc313x_clkevt.cpumask = cpumask_of(0);
	clockevents_register_device(&lpc313x_clkevt);

	lpc313x_clksrc.mult = clocksource_hz2mult(CLOCK_TICK_RATE, 
		lpc313x_clksrc.shift);
	clocksource_register(&lpc313x_clksrc);
}

struct sys_timer lpc313x_timer = {
	.init		= lpc313x_timer_init,
};


