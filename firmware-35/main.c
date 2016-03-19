#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/stm32/timer.h>

#define LED_PORT GPIOB
#define LED_PIN GPIO5

//extern uint32_t rcc_apb_frequency;
//extern uint32_t rcc_apb1_frequency;



int main(void)
{
    // Set clock to 35MHz
	rcc_osc_on(RCC_HSE);
	rcc_wait_for_osc_ready(RCC_HSE);
	rcc_set_sysclk_source(RCC_HSE);
	rcc_set_hpre(RCC_CFGR_HPRE_NODIV);
	rcc_set_ppre(RCC_CFGR_PPRE_NODIV);

	RCC_CFGR |= (1<<16);


	flash_set_ws(FLASH_ACR_LATENCY_024_048MHZ);
	rcc_set_pll_multiplication_factor(RCC_CFGR_PLLMUL_MUL10);


	rcc_apb1_frequency = 35400000;
	rcc_ahb_frequency = 35400000;

	rcc_osc_on(RCC_PLL);
	rcc_wait_for_osc_ready(RCC_PLL);
	rcc_set_sysclk_source(RCC_PLL);

/*

	RCC_CFGR |= 4 << 24;

	rcc_periph_clock_enable(RCC_GPIOA);
	gpio_mode_setup(GPIOA,GPIO_MODE_AF,GPIO_PUPD_NONE,GPIO8);
	gpio_set_af(GPIOA,0,GPIO8);
	gpio_set_output_options(GPIOA,GPIO_OTYPE_PP,GPIO_OSPEED_50MHZ,GPIO8);
*/






	rcc_periph_clock_enable(RCC_GPIOA);
	gpio_mode_setup(GPIOA,GPIO_MODE_AF,GPIO_PUPD_NONE,GPIO8|GPIO9|GPIO10|GPIO11);
	gpio_set_af(GPIOA,2,GPIO8|GPIO9|GPIO10|GPIO11);
	gpio_set_output_options(GPIOA,GPIO_OTYPE_PP,GPIO_OSPEED_100MHZ,GPIO8|GPIO9|GPIO10|GPIO11);



    // IMPORTANT: every peripheral must be clocked before use
    rcc_periph_clock_enable(RCC_GPIOB);

    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_TIM1);
    gpio_set_output_options(GPIOA, GPIO_OTYPE_PP,
    		GPIO_OSPEED_100MHZ, GPIO8|GPIO9|GPIO10|GPIO11);
    gpio_set_af(GPIOA,2,GPIO8|GPIO9|GPIO10|GPIO11);
    rcc_periph_clock_enable(RCC_TIM1);
    timer_reset(TIM1);
    timer_set_mode(TIM1, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_CENTER_1,
                   TIM_CR1_DIR_UP);
    timer_set_oc_mode(TIM1, TIM_OC1, TIM_OCM_PWM2);
    timer_set_oc_mode(TIM1, TIM_OC2, TIM_OCM_PWM2);
    timer_set_oc_mode(TIM1, TIM_OC3, TIM_OCM_PWM2);
    timer_set_oc_mode(TIM1, TIM_OC4, TIM_OCM_PWM2);
    timer_enable_oc_output(TIM1, TIM_OC1);
    timer_enable_oc_output(TIM1, TIM_OC2);
    timer_enable_oc_output(TIM1, TIM_OC3);
    timer_enable_oc_output(TIM1, TIM_OC4);
    timer_enable_break_main_output(TIM1);
    timer_set_oc_value(TIM1, TIM_OC1, 0);
    timer_set_oc_value(TIM1, TIM_OC2, 0);
    timer_set_oc_value(TIM1, TIM_OC3, 0);
    timer_set_oc_value(TIM1, TIM_OC4, 0);
    timer_set_period(TIM1, 1);
    timer_enable_counter(TIM1);

/**

    rcc_periph_clock_enable(RCC_TIM1);
    timer_reset(TIM1);
    timer_set_prescaler(TIM1, 1);
    timer_continuous_mode(TIM1);
    timer_set_mode(TIM1, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);

    timer_set_oc_mode(TIM1,TIM_OC1,TIM_OCM_TOGGLE);
    timer_enable_oc_output(TIM1, TIM_OC1);

    timer_enable_counter(TIM1);

TIM1_ARR = 50;
    TIM1_CCR1 = 30;
*/
    // Configure GPIO C.9 as an output
    //gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_PIN);

    // Flash the pin forever
    while(1)
    {
        gpio_set(LED_PORT, LED_PIN);
        int32_t i = 4800000;
        while(i)
            i--;
        gpio_clear(LED_PORT, LED_PIN);
        i = 4800000;
        while(i)
            i--;
    }
}
