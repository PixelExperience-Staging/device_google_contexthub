#include <plat/inc/gpio.h>
#include <plat/inc/usart.h>
#include <plat/inc/cmsis.h>
#include <plat/inc/pwr.h>
#include <plat/inc/rtc.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <platform.h>
#include <seos.h>
#include <timer.h>
#include <usart.h>
#include <gpio.h>
#include <mpu.h>
#include <cpu.h>


struct StmDbg {
    volatile uint32_t IDCODE;
    volatile uint32_t CR;
    volatile uint32_t APB1FZ;
    volatile uint32_t APB2FZ;
};

struct StmTim {

    volatile uint16_t CR1;
    uint8_t unused0[2];
    volatile uint16_t CR2;
    uint8_t unused1[2];
    volatile uint16_t SMCR;
    uint8_t unused2[2];
    volatile uint16_t DIER;
    uint8_t unused3[2];
    volatile uint16_t SR;
    uint8_t unused4[2];
    volatile uint16_t EGR;
    uint8_t unused5[2];
    volatile uint16_t CCMR1;
    uint8_t unused6[2];
    volatile uint16_t CCMR2;
    uint8_t unused7[2];
    volatile uint16_t CCER;
    uint8_t unused8[2];
    volatile uint32_t CNT;
    volatile uint16_t PSC;
    uint8_t unused9[2];
    volatile uint32_t ARR;
    volatile uint16_t RCR;
    uint8_t unused10[2];
    volatile uint32_t CCR1;
    volatile uint32_t CCR2;
    volatile uint32_t CCR3;
    volatile uint32_t CCR4;
    volatile uint16_t BDTR;
    uint8_t unused11[2];
    volatile uint16_t DCR;
    uint8_t unused12[2];
    volatile uint16_t DMAR;
    uint8_t unused13[2];
    volatile uint16_t OR;
    uint8_t unused14[2];
};


#ifdef DEBUG_UART_UNITNO
static struct usart mDbgUart;
#endif
static uint64_t mTicks = 0;



void platUninitialize(void)
{
#ifdef DEBUG_UART_UNITNO
    usartClose(&mDbgUart);
#endif
}

void platSleep(void)
{
    asm volatile ("wfi\n"
            "nop" :::"memory");
}

void platWake(void)
{
}

void platLogPutchar(char ch)
{
#ifdef DEBUG_UART_UNITNO
     usartPutchat(&mDbgUart, ch);
#endif
}

uint64_t platDisableInterrupts(void)
{
    return cpuIntsOff();
}

uint64_t platEnableInterrupts(void)
{
    return cpuIntsOn();
}

void platRestoreInterrupts(uint64_t state)
{
    cpuIntsRestore(state);
}

void platInitialize(void)
{
    const uint32_t debugStateInSleepMode = 0x00000007; /* debug in all modes */
    struct StmTim *block = (struct StmTim*)TIM2_BASE;
    struct StmDbg *dbg = (struct StmDbg*)DBG_BASE;
    uint32_t i;

    pwrSystemInit();

    //set ints up for a sane state
    for (i = 0; i < NUM_INTERRUPTS; i++) {
        NVIC_SetPriority(i, 2);
        NVIC_DisableIRQ(i);
        NVIC_ClearPendingIRQ(i);
    }

#ifdef DEBUG_UART_UNITNO
    /* Open mDbgUart on PA2 and PA3 */
    usartOpen(&mDbgUart, DEBUG_UART_UNITNO, DEBUG_UART_GPIO_TX, DEBUG_UART_GPIO_RX,
               115200, USART_DATA_BITS_8,
               USART_STOP_BITS_1_0, USART_PARITY_NONE,
               USART_FLOW_CONTROL_NONE);
#endif

    /* set up debugging */
#ifdef DEBUG
    dbg->CR |= debugStateInSleepMode;
#else
    dbg->CR &=~ debugStateInSleepMode;
#endif

    /* enable MPU */
    mpuStart();

    /* set up timer used for alarms */
    pwrUnitClock(PERIPH_BUS_APB1, PERIPH_APB1_TIM2, true);
    block->CR1 = (block->CR1 &~ 0x03E1) | 0x0010; //count down mode with no clock division, disabled
    block->PSC = 15; // prescale by 16, so that at 16MHz CPU clock, we get 1MHz timer
    block->DIER |= 1; // interrupt when updated (underflowed)
    NVIC_EnableIRQ(TIM2_IRQn);

    /* set up RTC */
    rtcInit();
}


void platSetAlarm(unsigned delayUs)
{
    struct StmTim *block = (struct StmTim*)TIM2_BASE;

    //better not have another one pending ad this moment
    block->CNT = delayUs;
    block->CR1 |= 1;
}

uint64_t platGetTicks(void)
{
    return mTicks;
}

/* Timer interrupt handler */
void TIM2_IRQHandler(void);
void TIM2_IRQHandler(void)
{
    struct StmTim *block = (struct StmTim*)TIM2_BASE;

    /* int clear */
    block->SR &=~ 1;

    /* timer off */
    block->CR1 &=~ 1;

    /* tell the caller */
    timIntHandler();
}

/* SysTick interrupt handler */
void SysTick_Handler(void);
void SysTick_Handler(void)
{
    mTicks++;
}

static void __attribute__((used)) logHardFault(uint32_t *excRegs, uint32_t* otherRegs)
{
    osLog(LOG_ERROR, "*HARD FAULT* SR  = %08lX\n", excRegs[7]);
    osLog(LOG_ERROR, "R0  = %08lX   R8  = %08lX\n", excRegs[0], otherRegs[4]);
    osLog(LOG_ERROR, "R1  = %08lX   R9  = %08lX\n", excRegs[1], otherRegs[5]);
    osLog(LOG_ERROR, "R2  = %08lX   R10 = %08lX\n", excRegs[2], otherRegs[6]);
    osLog(LOG_ERROR, "R3  = %08lX   R11 = %08lX\n", excRegs[3], otherRegs[7]);
    osLog(LOG_ERROR, "R4  = %08lX   R12 = %08lX\n", otherRegs[0], excRegs[4]);
    osLog(LOG_ERROR, "R5  = %08lX   SP  = %08lX\n", otherRegs[1], (uint32_t)(uintptr_t)(excRegs + 8));
    osLog(LOG_ERROR, "R6  = %08lX   LR  = %08lX\n", otherRegs[2], excRegs[5]);
    osLog(LOG_ERROR, "R7  = %08lX   PC  = %08lX\n", otherRegs[3], excRegs[6]);
    osLog(LOG_ERROR, "HFSR= %08lX   CFSR= %08lX\n", SCB->HFSR, SCB->CFSR);
    while(1);
}

void HardFault_Handler(void);

void __attribute__((naked)) HardFault_Handler(void)
{
    asm volatile(
        "tst lr, #4         \n"
        "ite eq             \n"
        "mrseq r0, msp      \n"
        "mrsne r0, psp      \n"
        "push  {r4-r11}     \n"
        "mov   r1, sp       \n"
        "b     logHardFault \n"
    );
}