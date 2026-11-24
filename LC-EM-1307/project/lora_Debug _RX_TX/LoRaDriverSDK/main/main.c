#include "hal/sx127x_hal.h"
#include "stm32f10x.h"
#include "string.h"

#define UART_BAUD    9600
#define BUF_SIZE     128
#define FRAME_GAP_MS 50

/* ?? stm32f10x_it.c — ??? ??? ???? SysTick_Handler ? g_u32SystickCount */
extern uint32_t GetTick(void);

/* --- USART1 (COM7) --- */
static volatile uint8_t  rx_buf1[BUF_SIZE];
static volatile uint8_t  rx_len1 = 0;
static volatile uint32_t rx_last_tick1 = 0;

/* --- USART2 (COM5) --- */
static volatile uint8_t  rx_buf2[BUF_SIZE];
static volatile uint8_t  rx_len2 = 0;
static volatile uint32_t rx_last_tick2 = 0;

/* ---------- RS485 direction ---------- */
static void rs485_set_tx(uint8_t enable_tx) {
  if (enable_tx) {
    GPIO_SetBits(GPIOB, GPIO_Pin_13);
    GPIO_SetBits(GPIOA, GPIO_Pin_8);
  } else {
    GPIO_ResetBits(GPIOB, GPIO_Pin_13);
    GPIO_ResetBits(GPIOA, GPIO_Pin_8);
  }
}

/* ---------- UART IRQ ---------- */
void USART1_IRQHandler(void) {
  if (USART_GetFlagStatus(USART1, USART_FLAG_ORE) != RESET) {
    (void)USART1->SR;
    (void)USART1->DR;
  }
  if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
    uint8_t b = (uint8_t)USART_ReceiveData(USART1);
    if (rx_len1 < BUF_SIZE) {
      rx_buf1[rx_len1++] = b;
    }
    rx_last_tick1 = GetTick();
  }
}

void USART2_IRQHandler(void) {
  if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) != RESET) {
    (void)USART2->SR;
    (void)USART2->DR;
  }
  if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
    uint8_t b = (uint8_t)USART_ReceiveData(USART2);
    if (rx_len2 < BUF_SIZE) {
      rx_buf2[rx_len2++] = b;
    }
    rx_last_tick2 = GetTick();
  }
}

/* ---------- UART init ---------- */
static void uartBridgeInit(void) {
  USART_InitTypeDef u;
  GPIO_InitTypeDef  g;
  NVIC_InitTypeDef  n;

  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

  RCC_APB2PeriphClockCmd(
      RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_USART1,
      ENABLE);
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

  /* RS485 DE/RE + LED */
  g.GPIO_Speed = GPIO_Speed_50MHz;
  g.GPIO_Mode  = GPIO_Mode_Out_PP;
  g.GPIO_Pin   = GPIO_Pin_13 | GPIO_Pin_12;
  GPIO_Init(GPIOB, &g);

  g.GPIO_Pin = GPIO_Pin_8;
  GPIO_Init(GPIOA, &g);

  rs485_set_tx(0);

  /* USART1 TX=PA9, USART2 TX=PA2 */
  g.GPIO_Pin  = GPIO_Pin_9 | GPIO_Pin_2;
  g.GPIO_Mode = GPIO_Mode_AF_PP;
  GPIO_Init(GPIOA, &g);

  /* USART1 RX=PA10, USART2 RX=PA3 */
  g.GPIO_Pin  = GPIO_Pin_10 | GPIO_Pin_3;
  g.GPIO_Mode = GPIO_Mode_IN_FLOATING;
  GPIO_Init(GPIOA, &g);

  u.USART_BaudRate            = UART_BAUD;
  u.USART_WordLength          = USART_WordLength_8b;
  u.USART_StopBits            = USART_StopBits_1;
  u.USART_Parity              = USART_Parity_No;
  u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
  u.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;

  USART_Init(USART1, &u);
  USART_Init(USART2, &u);

  USART_ClearFlag(USART1, USART_FLAG_RXNE | USART_FLAG_TC | USART_FLAG_ORE);
  USART_ClearFlag(USART2, USART_FLAG_RXNE | USART_FLAG_TC | USART_FLAG_ORE);

  USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
  USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);

  n.NVIC_IRQChannel                   = USART1_IRQn;
  n.NVIC_IRQChannelPreemptionPriority = 0;
  n.NVIC_IRQChannelSubPriority        = 0;
  n.NVIC_IRQChannelCmd                = ENABLE;
  NVIC_Init(&n);

  n.NVIC_IRQChannel            = USART2_IRQn;
  n.NVIC_IRQChannelSubPriority = 1;
  NVIC_Init(&n);

  USART_Cmd(USART1, ENABLE);
  USART_Cmd(USART2, ENABLE);
}

/* ---------- UART send to both ports ---------- */
static void uartSendBuffer(const uint8_t *buf, uint8_t len) {
  if (len == 0) return;

  rs485_set_tx(1);
  Soft_delay_ms(5);          /* ???? 2 — ????????? ??? ?????????? RS485 */

  for (uint8_t i = 0; i < len; i++) {
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, buf[i]);

    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
    USART_SendData(USART2, buf[i]);
  }
  while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
  while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);

  Soft_delay_ms(5);
  rs485_set_tx(0);
}

/* ---------- SX127x SPI helpers ---------- */
static void sxWrite(uint8_t addr, uint8_t val) {
  SpiNSSEnable(0);
  SpiInOut(addr | 0x80);
  SpiInOut(val);
  SpiNSSEnable(1);
}

static uint8_t sxRead(uint8_t addr) {
  SpiNSSEnable(0);
  SpiInOut(addr & 0x7F);
  uint8_t val = SpiInOut(0x00);
  SpiNSSEnable(1);
  return val;
}

/* ---------- LoRa init 433 MHz ---------- */
static uint8_t loraInit433(void) {
  SX127X_ResetPinControl(0);
  Soft_delay_ms(15);
  SX127X_ResetPinControl(1);
  Soft_delay_ms(20);

  if (sxRead(0x42) != 0x12) return 0;   /* RegVersion */

  sxWrite(0x01, 0x80);   /* Sleep + LoRa */
  Soft_delay_ms(10);
  sxWrite(0x01, 0x81);   /* Standby */
  Soft_delay_ms(10);

  sxWrite(0x06, 0x6C);   /* FrfMsb ? 433.0 MHz */
  sxWrite(0x07, 0x40);
  sxWrite(0x08, 0x00);

  sxWrite(0x09, 0x8F);   /* PaConfig: PA_BOOST, +17 dBm */
  sxWrite(0x0B, 0x2B);   /* OcpTrim */
  sxWrite(0x0C, 0x23);   /* LnaConfig */

  sxWrite(0x1D, 0x72);   /* ModemConfig2: SF7, CRC on */
  sxWrite(0x1E, 0x74);   /* ModemConfig3 */
  sxWrite(0x26, 0x04);   /* ModemConfig... invertIQ off */

  sxWrite(0x20, 0x00);   /* PreambleMsb */
  sxWrite(0x21, 0x08);   /* PreambleLsb = 8 */
  sxWrite(0x39, 0x12);   /* SyncWord = 0x12 */

  sxWrite(0x0E, 0x00);   /* FifoTxBaseAddr */
  sxWrite(0x0F, 0x00);   /* FifoRxBaseAddr */
  sxWrite(0x40, 0x00);   /* DIO mapping */

  sxWrite(0x12, 0xFF);   /* Clear IRQ */
  sxWrite(0x01, 0x85);   /* RXCONTINUOUS */
  return 1;
}

/* ---------- LoRa TX ---------- */
static void loraSendPacket(const uint8_t *buf, uint8_t len) {
  if (len == 0) return;

  GPIO_SetBits(GPIOB, GPIO_Pin_12);   /* LED TX */

  sxWrite(0x01, 0x81);    /* Standby */
  sxWrite(0x12, 0xFF);    /* Clear IRQ */
  sxWrite(0x0D, 0x00);    /* FifoAddrPtr = TxBase */

  SpiNSSEnable(0);
  SpiInOut(0x80);         /* FIFO Write */
  for (uint8_t i = 0; i < len; i++) {
    SpiInOut(buf[i]);
  }
  SpiNSSEnable(1);

  sxWrite(0x22, len);     /* PayloadLength */
  sxWrite(0x01, 0x83);    /* TX */

  uint16_t timeout = 500;
  while (((sxRead(0x12) & 0x08) == 0) && (timeout > 0)) {
    Soft_delay_ms(1);
    timeout--;
  }

  sxWrite(0x12, 0xFF);
  sxWrite(0x01, 0x85);    /* ????????? ? RXCONTINUOUS */
  GPIO_ResetBits(GPIOB, GPIO_Pin_12);
}

/* ---------- main ---------- */
int main(void) {
  uint8_t tx_copy[BUF_SIZE];
  uint8_t radio_rx[BUF_SIZE];

  SysTick_Config(SystemCoreClock / 1000);   /* 1 ms tick, ?????????? ? stm32f10x_it.c */
  uartBridgeInit();
  SX1276HALInit();

  while (!loraInit433()) {
    GPIO_SetBits(GPIOB, GPIO_Pin_12);
    Soft_delay_ms(100);
    GPIO_ResetBits(GPIOB, GPIO_Pin_12);
    Soft_delay_ms(100);
  }

  uartSendBuffer((const uint8_t *)"BRIDGE_READY\r\n", 14);
  for (int i = 0; i < 2; i++) {
    GPIO_SetBits(GPIOB, GPIO_Pin_12);
    Soft_delay_ms(120);
    GPIO_ResetBits(GPIOB, GPIO_Pin_12);
    Soft_delay_ms(120);
  }

  while (1) {
    /* 1. ????? ?? USART1 (COM7) */
    if (rx_len1 > 0 &&
        ((GetTick() - rx_last_tick1) >= FRAME_GAP_MS || rx_len1 >= BUF_SIZE)) {
      __disable_irq();
      uint8_t len = rx_len1;
      for (uint8_t i = 0; i < len; i++) tx_copy[i] = rx_buf1[i];
      rx_len1 = 0;
      __enable_irq();

      loraSendPacket(tx_copy, len);
    }

    /* 2. ????? ?? USART2 (COM5) */
    if (rx_len2 > 0 &&
        ((GetTick() - rx_last_tick2) >= FRAME_GAP_MS || rx_len2 >= BUF_SIZE)) {
      __disable_irq();
      uint8_t len = rx_len2;
      for (uint8_t i = 0; i < len; i++) tx_copy[i] = rx_buf2[i];
      rx_len2 = 0;
      __enable_irq();

      loraSendPacket(tx_copy, len);
    }

    /* 3. ????? LoRa */
    uint8_t irq = sxRead(0x12);
    if (irq & 0x40) {                       /* RxDone */
      uint8_t len      = sxRead(0x13);      /* RxNbBytes */
      uint8_t fifo_addr = sxRead(0x10);     /* FifoRxCurrentAddr */

      if (len > BUF_SIZE) len = BUF_SIZE;
      sxWrite(0x0D, fifo_addr);

      SpiNSSEnable(0);
      SpiInOut(0x00);                       /* FIFO Read */
      for (uint8_t i = 0; i < len; i++) {
        radio_rx[i] = SpiInOut(0x00);
      }
      SpiNSSEnable(1);
      sxWrite(0x12, 0xFF);

      if (!(irq & 0x20) && len > 0) {       /* PayloadCrcError == 0 */
        for (int k = 0; k < 3; k++) {
          GPIO_SetBits(GPIOB, GPIO_Pin_12);
          Soft_delay_ms(20);
          GPIO_ResetBits(GPIOB, GPIO_Pin_12);
          Soft_delay_ms(20);
        }
        uartSendBuffer(radio_rx, len);
      }
    }
  }
}