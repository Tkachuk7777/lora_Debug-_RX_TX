#include "stm32f10x.h"
#include "hal/sx127x_hal.h"
#include "string.h"
#include "stm32f10x_iwdg.h"
#include <stdio.h>


#define UART_BAUD        9600
#define BUF_SIZE         128
#define FRAME_GAP_MS     50
#define LORA_FREQ_434    0x6C8000UL

/* --- DWT for precise delays --- */
#define DWT_CTRL    (*(volatile uint32_t *)0xE0001000)
#define DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004)
#define DEMCR       (*(volatile uint32_t *)0xE000EDFC)

void DWT_Init(void) {
    DEMCR |= (1UL << 24);
    DWT_CYCCNT = 0;
    DWT_CTRL |= (1UL << 0);
}

void delay_us(uint32_t us) {
    uint32_t start = DWT_CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000);
    while ((DWT_CYCCNT - start) < ticks);
}

/* --- System tick (provided by stm32f10x_it.c) --- */
extern uint32_t GetTick(void);

/* --- IWDG --- */
void IWDG_Init(void) {
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    IWDG_SetReload(1250);
    IWDG_ReloadCounter();
    IWDG_Enable();
}
void IWDG_Feed(void) {
    IWDG_ReloadCounter();
}

/* --- UART buffers --- */
static volatile uint8_t  rx_buf2[BUF_SIZE];
static volatile uint8_t  rx_len2 = 0;
static volatile uint32_t rx_last_tick2 = 0;
static uint8_t lora_rx_buf[BUF_SIZE];
static uint8_t tx_buf[BUF_SIZE];

/* --- LED blink --- */
volatile uint8_t  led_active = 0;
volatile uint32_t led_on_tick = 0;

/* --- RS485 control --- */
static void rs485_set_tx(uint8_t enable_tx) {
    if (enable_tx) {
        GPIO_SetBits(GPIOB, GPIO_Pin_13);
        GPIO_SetBits(GPIOA, GPIO_Pin_8);
        delay_us(10);
    } else {
        delay_us(10);
        GPIO_ResetBits(GPIOB, GPIO_Pin_13);
        GPIO_ResetBits(GPIOA, GPIO_Pin_8);
    }
}

/* --- USART2 IRQ --- */
void USART2_IRQHandler(void) {
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
        uint8_t b = (uint8_t)USART_ReceiveData(USART2);
        if (rx_len2 < BUF_SIZE) rx_buf2[rx_len2++] = b;
        rx_last_tick2 = GetTick();
    } else if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) != RESET) {
        (void)USART2->SR;
        (void)USART2->DR;
    }
}

/* --- UART init --- */
static void uart_init(void) {
    USART_InitTypeDef u;
    GPIO_InitTypeDef g;
    NVIC_InitTypeDef n;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_Mode  = GPIO_Mode_Out_PP;
    g.GPIO_Pin   = GPIO_Pin_13 | GPIO_Pin_12;
    GPIO_Init(GPIOB, &g);
    g.GPIO_Pin   = GPIO_Pin_8;
    GPIO_Init(GPIOA, &g);
    rs485_set_tx(0);

    g.GPIO_Mode = GPIO_Mode_AF_PP;
    g.GPIO_Pin  = GPIO_Pin_2;
    GPIO_Init(GPIOA, &g);
    g.GPIO_Mode = GPIO_Mode_IPU;
    g.GPIO_Pin  = GPIO_Pin_3;
    GPIO_Init(GPIOA, &g);

    u.USART_BaudRate            = UART_BAUD;
    u.USART_WordLength          = USART_WordLength_8b;
    u.USART_StopBits            = USART_StopBits_1;
    u.USART_Parity              = USART_Parity_No;
    u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    u.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;

    USART_Init(USART2, &u);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);

    n.NVIC_IRQChannel                   = USART2_IRQn;
    n.NVIC_IRQChannelPreemptionPriority = 0;
    n.NVIC_IRQChannelSubPriority        = 0;
    n.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&n);

    USART_Cmd(USART2, ENABLE);
}

/* --- UART send via RS485 --- */
static void uart_send_rs485(const uint8_t *buf, uint8_t len) {
    if (len == 0) return;
    rs485_set_tx(1);

    for (uint8_t i = 0; i < len; i++) {
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
        USART_SendData(USART2, buf[i]);
    }
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);

    rs485_set_tx(0);
}

/* --- SPI helpers --- */
static void sxWrite(uint8_t addr, uint8_t val) {
    SpiNSSEnable(0);
    SpiInOut(addr | 0x80);
    SpiInOut(val);
    SpiNSSEnable(1);
}

static uint8_t sxRead(uint8_t addr) {
    SpiNSSEnable(0);
    SpiInOut(addr & 0x7F);
    uint8_t v = SpiInOut(0);
    SpiNSSEnable(1);
    return v;
}

/* --- LoRa init --- */
static uint8_t lora_init(void) {
    SX127X_ResetPinControl(0);
    Soft_delay_ms(15);
    SX127X_ResetPinControl(1);
    Soft_delay_ms(20);

    if (sxRead(0x42) != 0x12) return 0;

    sxWrite(0x01, 0x80);
    Soft_delay_ms(10);
    sxWrite(0x01, 0x81);
    Soft_delay_ms(10);

    sxWrite(0x06, (LORA_FREQ_434 >> 16) & 0xFF);
    sxWrite(0x07, (LORA_FREQ_434 >> 8)  & 0xFF);
    sxWrite(0x08,  LORA_FREQ_434        & 0xFF);

    /* PA_BOOST, 17 dBm */
    sxWrite(0x09, 0x8F);
    sxWrite(0x0B, 0x2B);
    sxWrite(0x0C, 0x23);
    sxWrite(0x1D, 0x72);
    sxWrite(0x1E, 0x74);
    sxWrite(0x26, 0x04);
    sxWrite(0x20, 0x00);
    sxWrite(0x21, 0x08);
    sxWrite(0x39, 0x12);
    sxWrite(0x0E, 0x00);
    sxWrite(0x0F, 0x00);
    sxWrite(0x40, 0x00);
    sxWrite(0x12, 0xFF);
    sxWrite(0x01, 0x85);

    return 1;
}

/* --- LoRa send --- */
static void lora_send(const uint8_t *buf, uint8_t len) {
    if (len == 0) return;

    GPIO_SetBits(GPIOB, GPIO_Pin_12);

    sxWrite(0x01, 0x81);
    sxWrite(0x12, 0xFF);
    sxWrite(0x0D, 0x00);

    SpiNSSEnable(0);
    SpiInOut(0x80);
    for (uint8_t i = 0; i < len; i++) {
        SpiInOut(buf[i]);
    }
    SpiNSSEnable(1);

    sxWrite(0x22, len);
    sxWrite(0x01, 0x83);

    uint16_t timeout = 1000;
    while (((sxRead(0x12) & 0x08) == 0) && (timeout > 0)) {
        Soft_delay_ms(1);
        timeout--;
    }

    sxWrite(0x12, 0xFF);
    sxWrite(0x01, 0x85);

    GPIO_ResetBits(GPIOB, GPIO_Pin_12);
}

/* --- LoRa receive with diagnostics --- */
static uint8_t lora_receive(void) {
    uint8_t irq = sxRead(0x12);

    /* Diagnostic: print IRQ status if not zero */
    if (irq != 0) {
        char dbg[32];
        int n = sprintf(dbg, "IRQ:0x%02X\r\n", irq);
        uart_send_rs485((uint8_t*)dbg, n);
        sxWrite(0x12, 0xFF);
        return 0; /* Don't process packet, just report */
    }

    return 0;
}

/* --- Main --- */
int main(void) {
    SystemInit();
    DWT_Init();
    SysTick_Config(SystemCoreClock / 1000);
    uart_init();
    SX1276HALInit();

    while (!lora_init()) {
        GPIO_SetBits(GPIOB, GPIO_Pin_12);
        Soft_delay_ms(100);
        GPIO_ResetBits(GPIOB, GPIO_Pin_12);
        Soft_delay_ms(100);
    }

    const char ready[] = "BRIDGE_READY\r\n";
    uart_send_rs485((const uint8_t *)ready, sizeof(ready) - 1);

    for (int i = 0; i < 2; i++) {
        GPIO_SetBits(GPIOB, GPIO_Pin_12);
        Soft_delay_ms(120);
        GPIO_ResetBits(GPIOB, GPIO_Pin_12);
        Soft_delay_ms(120);
    }

    IWDG_Init();

    while (1) {
        uint32_t now = GetTick();

        /* 1. USART2 -> LoRa */
        uint8_t len2 = 0;
        __disable_irq();
        if (rx_len2 > 0 && (now - rx_last_tick2 >= FRAME_GAP_MS || rx_len2 >= BUF_SIZE)) {
            len2 = rx_len2;
            memcpy(tx_buf, (const void *)rx_buf2, len2);
            rx_len2 = 0;
        }
        __enable_irq();

        if (len2 > 0) {
            lora_send(tx_buf, len2);
        }

        /* 2. LoRa receive (only diagnostic) */
        lora_receive();

        /* 3. LED off timer */
        if (led_active && (GetTick() - led_on_tick >= 100)) {
            GPIO_ResetBits(GPIOB, GPIO_Pin_12);
            led_active = 0;
        }

        /* 4. Watchdog */
        IWDG_Feed();
    }
}