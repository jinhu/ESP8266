/******************************************************************************
 * Copyright 2013-2014 Espressif Systems (Wuxi)
 *
 * FileName: uart.c
 *
 * Description: Two UART mode configration and interrupt handler.
 *              Check your hardware connection while use this mode.
 *
 * Modification history:
 *     2014/3/12, v1.0 create this file.
*******************************************************************************/
#include "ets_sys.h"
#include "osapi.h"
#include "driver/uart.h"

#define UART0   0
#define UART1   1

// UartDev is defined and initialized in rom code.
extern UartDevice UartDev;

LOCAL void uart0_rx_intr_handler(void *para);
char readOutBuffer[RX_BUFF_SIZE] = { 0 };
/******************************************************************************
 * FunctionName : uart_config
 * Description  : Internal used function
 *                UART0 used for data TX/RX, RX buffer size is 0x100, interrupt enabled
 *                UART1 just used for debug output
 * Parameters   : uart_no, use UART0 or UART1 defined ahead
 * Returns      : NONE
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
uart_config(uint8 uart_no)
{
    if (uart_no == UART1) {
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_U1TXD_BK);
    } else {
        /* rcv_buff size if 0x100 */
        ETS_UART_INTR_ATTACH(uart0_rx_intr_handler,  &(UartDev.rcv_buff));
        PIN_PULLUP_DIS(PERIPHS_IO_MUX_U0TXD_U);
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_U0TXD);
    }

    uart_div_modify(uart_no, UART_CLK_FREQ / (UartDev.baut_rate));

    WRITE_PERI_REG(UART_CONF0(uart_no),    UartDev.exist_parity
                   | UartDev.parity
                   | (UartDev.stop_bits << UART_STOP_BIT_NUM_S)
                   | (UartDev.data_bits << UART_BIT_NUM_S));


    //clear rx and tx fifo,not ready
    SET_PERI_REG_MASK(UART_CONF0(uart_no), UART_RXFIFO_RST | UART_TXFIFO_RST);
    CLEAR_PERI_REG_MASK(UART_CONF0(uart_no), UART_RXFIFO_RST | UART_TXFIFO_RST);

    //set rx fifo trigger
    WRITE_PERI_REG(UART_CONF1(uart_no), (UartDev.rcv_buff.TrigLvl & UART_RXFIFO_FULL_THRHD) << UART_RXFIFO_FULL_THRHD_S);

    //clear all interrupt
    WRITE_PERI_REG(UART_INT_CLR(uart_no), 0xffff);
    //enable rx_interrupt
    SET_PERI_REG_MASK(UART_INT_ENA(uart_no), UART_RXFIFO_FULL_INT_ENA);
}

/******************************************************************************
 * FunctionName : uart1_tx_one_char
 * Description  : Internal used function
 *                Use uart1 interface to transfer one char
 * Parameters   : uint8 TxChar - character to tx
 * Returns      : OK
*******************************************************************************/
LOCAL STATUS ICACHE_FLASH_ATTR
uart0_tx_one_char(uint8 TxChar)
{
    while (true)
	{
		uint32 fifo_cnt = READ_PERI_REG(UART_STATUS(UART0)) & (UART_TXFIFO_CNT<<UART_TXFIFO_CNT_S);
		if ((fifo_cnt >> UART_TXFIFO_CNT_S & UART_TXFIFO_CNT) < 126) {
			break;
		}
	}

	WRITE_PERI_REG(UART_FIFO(UART0) , TxChar);
	return OK;
}

/******************************************************************************
 * FunctionName : uart1_write_char
 * Description  : Internal used function
 *                Do some special deal while tx char is '\r' or '\n'
 * Parameters   : char c - character to tx
 * Returns      : NONE
*******************************************************************************/
LOCAL void ICACHE_FLASH_ATTR
uart1_write_char(char c)
{ }

LOCAL void ICACHE_FLASH_ATTR
uart0_write_char(char c)
{
    if (c == '\n') {
        uart0_tx_one_char('\r');
        uart0_tx_one_char('\n');
    } else if (c == '\r') {
    } else {
        uart0_tx_one_char(c);
    }
}
uart_recv_line  uart_recv_line_cb;

/******************************************************************************
 * FunctionName : uart0_rx_intr_handler
 * Description  : Internal used function
 *                UART0 interrupt handler, add self handle code inside
 * Parameters   : void *para - point to ETS_UART_INTR_ATTACH's arg
 * Returns      : NONE
*******************************************************************************/
LOCAL void
uart0_rx_intr_handler(void *para)
{
    /* uart0 and uart1 intr combine togther, when interrupt occur, see reg 0x3ff20020, bit2, bit0 represents
     * uart1 and uart0 respectively
     */
    RcvMsgBuff *pRxBuff = (RcvMsgBuff *)para;
    uint8 RcvChar;

    if (UART_RXFIFO_FULL_INT_ST != (READ_PERI_REG(UART_INT_ST(UART0)) & UART_RXFIFO_FULL_INT_ST)) {
        return;
    }

    WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_FULL_INT_CLR);

    while (READ_PERI_REG(UART_STATUS(UART0)) & (UART_RXFIFO_CNT << UART_RXFIFO_CNT_S)) {
        RcvChar = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
        if (RcvChar == '\r') {
            pRxBuff->pWritePos++;
            *(pRxBuff->pWritePos) = '\0';
        	pRxBuff->BuffState = WRITE_OVER;
        	pRxBuff->pWritePos = pRxBuff->pRcvMsgBuff;
            os_memset(readOutBuffer, 0, RX_BUFF_SIZE);
        	os_memcpy(readOutBuffer, pRxBuff->pRcvMsgBuff, os_strlen(pRxBuff->pRcvMsgBuff));
            os_memset(pRxBuff->pRcvMsgBuff, 0, RX_BUFF_SIZE);
            uart_recv_line_cb(readOutBuffer);
        } else {
            *(pRxBuff->pWritePos) = RcvChar;
            pRxBuff->pWritePos++;
        }

        if (pRxBuff->pWritePos == (pRxBuff->pRcvMsgBuff + RX_BUFF_SIZE)) {
            // overflow ...we may need more error handle here.
            pRxBuff->pWritePos = pRxBuff->pRcvMsgBuff ;
        }
    }
}


/******************************************************************************
 * FunctionName : uart0_tx_buffer
 * Description  : use uart0 to transfer buffer
 * Parameters   : uint8 *buf - point to send buffer
 *                uint16 len - buffer len
 * Returns      :
*******************************************************************************/
void ICACHE_FLASH_ATTR
uart0_tx_buffer(uint8 *buf, uint16 len)
{
    uint16 i;

    for (i = 0; i < len; i++) {
        uart_tx_one_char(buf[i]);
    }
}
/******************************************************************************
 * FunctionName : uart_init
 * Description  : user interface for init uart
 * Parameters   : UartBautRate uart0_br - uart0 bautrate
 *                UartBautRate uart1_br - uart1 bautrate
 * Returns      : NONE
*******************************************************************************/
void ICACHE_FLASH_ATTR
uart_init(UartBautRate uart0_br, uart_recv_line uart_recv_line_cb_tmp, bool debugprint)
{
    // rom use 74880 baut_rate, here reinitialize
    UartDev.baut_rate = uart0_br;
    uart_config(UART0);
    char* buff = (UartDev.rcv_buff.pRcvMsgBuff);
    os_memset(buff, 0, RX_BUFF_SIZE);
    ETS_UART_INTR_ENABLE();
    uart_recv_line_cb = uart_recv_line_cb_tmp;
    // install uart1 putc callback
    if(debugprint) {
    	os_install_putc1((void *)uart0_write_char);
    } else {
    	os_install_putc1((void *)uart1_write_char);
    }
}
