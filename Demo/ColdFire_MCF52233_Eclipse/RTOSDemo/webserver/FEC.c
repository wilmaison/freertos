/*
	FreeRTOS.org V5.1.0 - Copyright (C) 2003-2008 Richard Barry.

	This file is part of the FreeRTOS.org distribution.

	FreeRTOS.org is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	FreeRTOS.org is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with FreeRTOS.org; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

	A special exception to the GPL can be applied should you wish to distribute
	a combined work that includes FreeRTOS.org, without being obliged to provide
	the source code for any proprietary components.  See the licensing section
	of http://www.FreeRTOS.org for full details of how and when the exception
	can be applied.

	***************************************************************************
	See http://www.FreeRTOS.org for documentation, latest information, license
	and contact details.  Please ensure to read the configuration and relevant
	port sections of the online documentation.
	***************************************************************************
*/

/* Kernel includes. */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* Demo includes. */
#include "fecbd.h"
#include "mii.h"
#include "eth_phy.h"
#include "eth.h"

/* uIP includes. */
#include "uip.h"
#include "uip_arp.h"

#define MCF_FEC_MSCR_MII_SPEED(x)            	(((x)&0x3F)<<0x1)
#define MCF_FEC_RCR_MAX_FL(x)                	(((x)&0x7FF)<<0x10)

/* Delay between polling the PHY to see if a link has been established. */
#define fecLINK_DELAY							( 500 / portTICK_RATE_MS )

/* Delay to wait for an MII access. */
#define fecMII_DELAY							( 10 / portTICK_RATE_MS )
#define fecMAX_POLLS							( 20 )

/* Delay between looking for incoming packets.  In ideal world this would be
infinite. */
#define netifBLOCK_TIME_WAITING_FOR_INPUT		fecLINK_DELAY

/* Constants used to delay while waiting for a tx descriptor to be free. */
#define fecTX_BUFFER_WAIT						( 200 / portTICK_RATE_MS )

/* We only use a single Tx descriptor - the duplicate send silicon errata
actually assists in this case. */
#define fecNUM_FEC_TX_BUFFERS					( 1 )
#define fecTX_BUFFER_TO_USE						( 0 )
/*-----------------------------------------------------------*/

/* The semaphore used to wake the uIP task when data arrives. */
xSemaphoreHandle xFECSemaphore = NULL, xTxSemaphore = NULL;

/* The buffer used by the uIP stack.  In this case the pointer is used to
point to one of the Rx buffers. */
unsigned portCHAR *uip_buf;

/* The DMA descriptors.  This is a char array to allow us to align it correctly. */
static unsigned portCHAR xFECTxDescriptors_unaligned[ ( fecNUM_FEC_TX_BUFFERS * sizeof( FECBD ) ) + 16 ];
static unsigned portCHAR xFECRxDescriptors_unaligned[ ( configNUM_FEC_RX_BUFFERS * sizeof( FECBD ) ) + 16 ];
static FECBD *xFECTxDescriptors;
static FECBD *xFECRxDescriptors;

/* The DMA buffers.  These are char arrays to allow them to be alligned correctly. */
static unsigned portCHAR ucFECRxBuffers[ ( configNUM_FEC_RX_BUFFERS * configFEC_BUFFER_SIZE ) + 16 ];
static unsigned portBASE_TYPE uxNextRxBuffer = 0, uxIndexToBufferOwner = 0;

/*-----------------------------------------------------------*/

static void prvEnableFECInterrupts( void );
static void prvResetFEC( portBASE_TYPE xCalledFromISR );

/********************************************************************/
/*
 * Write a value to a PHY's MII register.
 *
 * Parameters:
 *  ch          FEC channel
 *  phy_addr    Address of the PHY.
 *  reg_addr    Address of the register in the PHY.
 *  data        Data to be written to the PHY register.
 *
 * Return Values:
 *  0 on failure
 *  1 on success.
 *
 * Please refer to your PHY manual for registers and their meanings.
 * mii_write() polls for the FEC's MII interrupt event and clears it.
 * If after a suitable amount of time the event isn't triggered, a
 * value of 0 is returned.
 */
static int fec_mii_write( int phy_addr, int reg_addr, int data )
{
int timeout, iReturn;
uint32 eimr;

    /* Clear the MII interrupt bit */
    MCF_FEC_EIR = MCF_FEC_EIR_MII;

    /* Mask the MII interrupt */
    eimr = MCF_FEC_EIMR;
    MCF_FEC_EIMR &= ~MCF_FEC_EIMR_MII;

    /* Write to the MII Management Frame Register to kick-off the MII write */
    MCF_FEC_MMFR = MCF_FEC_MMFR_ST_01 | MCF_FEC_MMFR_OP_WRITE | MCF_FEC_MMFR_PA(phy_addr) | MCF_FEC_MMFR_RA(reg_addr) | MCF_FEC_MMFR_TA_10 | MCF_FEC_MMFR_DATA( data );

    /* Poll for the MII interrupt (interrupt should be masked) */
    for( timeout = 0; timeout < fecMAX_POLLS; timeout++ )
    {
        if( MCF_FEC_EIR & MCF_FEC_EIR_MII )
        {
			break;
        }
        else
        {
        	vTaskDelay( fecMII_DELAY );
        }
    }

    if( timeout == fecMAX_POLLS )
    {
        iReturn = 0;
    }
    else
    {
		iReturn = 1;
    }

	/* Clear the MII interrupt bit */
	MCF_FEC_EIR = MCF_FEC_EIR_MII;

	/* Restore the EIMR */
	MCF_FEC_EIMR = eimr;

    return iReturn;
}

/********************************************************************/
/*
 * Read a value from a PHY's MII register.
 *
 * Parameters:
 *  ch          FEC channel
 *  phy_addr    Address of the PHY.
 *  reg_addr    Address of the register in the PHY.
 *  data        Pointer to storage for the Data to be read
 *              from the PHY register (passed by reference)
 *
 * Return Values:
 *  0 on failure
 *  1 on success.
 *
 * Please refer to your PHY manual for registers and their meanings.
 * mii_read() polls for the FEC's MII interrupt event and clears it.
 * If after a suitable amount of time the event isn't triggered, a
 * value of 0 is returned.
 */
static int fec_mii_read( int phy_addr, int reg_addr, unsigned portSHORT* data )
{
int timeout, iReturn;
uint32 eimr;

    /* Clear the MII interrupt bit */
    MCF_FEC_EIR = MCF_FEC_EIR_MII;

    /* Mask the MII interrupt */
    eimr = MCF_FEC_EIMR;
    MCF_FEC_EIMR &= ~MCF_FEC_EIMR_MII;

    /* Write to the MII Management Frame Register to kick-off the MII read */
    MCF_FEC_MMFR = MCF_FEC_MMFR_ST_01 | MCF_FEC_MMFR_OP_READ | MCF_FEC_MMFR_PA(phy_addr) | MCF_FEC_MMFR_RA(reg_addr) | MCF_FEC_MMFR_TA_10;

    /* Poll for the MII interrupt (interrupt should be masked) */
    for( timeout = 0; timeout < fecMAX_POLLS; timeout++ )
    {
        if (MCF_FEC_EIR & MCF_FEC_EIR_MII)
        {
            break;
        }
        else
        {
        	vTaskDelay( fecMII_DELAY );
        }
    }

    if( timeout == fecMAX_POLLS )
    {
        iReturn = 0;
    }
    else
    {
		*data = (uint16)(MCF_FEC_MMFR & 0x0000FFFF);
		iReturn = 1;
    }

	/* Clear the MII interrupt bit */
	MCF_FEC_EIR = MCF_FEC_EIR_MII;

	/* Restore the EIMR */
	MCF_FEC_EIMR = eimr;

    return iReturn;
}


/********************************************************************/
/*
 * Generate the hash table settings for the given address
 *
 * Parameters:
 *  addr    48-bit (6 byte) Address to generate the hash for
 *
 * Return Value:
 *  The 6 most significant bits of the 32-bit CRC result
 */
static unsigned portCHAR fec_hash_address( const unsigned portCHAR* addr )
{
unsigned portLONG crc;
unsigned portCHAR byte;
int i, j;

	crc = 0xFFFFFFFF;
	for(i=0; i<6; ++i)
	{
		byte = addr[i];
		for(j=0; j<8; ++j)
		{
			if((byte & 0x01)^(crc & 0x01))
			{
				crc >>= 1;
				crc = crc ^ 0xEDB88320;
			}
			else
			{
				crc >>= 1;
			}

			byte >>= 1;
		}
	}

	return (unsigned portCHAR)(crc >> 26);
}

/********************************************************************/
/*
 * Set the Physical (Hardware) Address and the Individual Address
 * Hash in the selected FEC
 *
 * Parameters:
 *  ch  FEC channel
 *  pa  Physical (Hardware) Address for the selected FEC
 */
static void fec_set_address( const unsigned portCHAR *pa )
{
	unsigned portCHAR crc;

	/*
	* Set the Physical Address
	*/
	/* Set the source address for the controller */
	MCF_FEC_PALR = ( pa[ 0 ] << 24 ) | ( pa[ 1 ] << 16 ) | ( pa[ 2 ] << 8 ) | ( pa[ 3 ] << 0 );
	MCF_FEC_PAUR = ( pa[ 4 ] << 24 ) | ( pa[ 5 ] << 16 );

	/*
	* Calculate and set the hash for given Physical Address
	* in the  Individual Address Hash registers
	*/
	crc = fec_hash_address( pa );
	if( crc >= 32 )
	{
		MCF_FEC_IAUR |= (unsigned portLONG)(1 << (crc - 32));
	}
	else
	{
		MCF_FEC_IALR |= (unsigned portLONG)(1 << crc);
	}
}
/*-----------------------------------------------------------*/

static void prvInitialiseFECBuffers( void )
{
unsigned portBASE_TYPE ux;
unsigned portCHAR *pcBufPointer;

	pcBufPointer = &( xFECTxDescriptors_unaligned[ 0 ] );
	while( ( ( unsigned portLONG ) pcBufPointer & 0x0fUL ) != 0 )
	{
		pcBufPointer++;
	}

	xFECTxDescriptors = ( FECBD * ) pcBufPointer;

	pcBufPointer = &( xFECRxDescriptors_unaligned[ 0 ] );
	while( ( ( unsigned portLONG ) pcBufPointer & 0x0fUL ) != 0 )
	{
		pcBufPointer++;
	}

	xFECRxDescriptors = ( FECBD * ) pcBufPointer;


	/* Setup the buffers and descriptors.  The data member does not point
	anywhere yet as there is not yet anything to send and a zero copy policy
	is used. */
	for( ux = 0; ux < fecNUM_FEC_TX_BUFFERS; ux++ )
	{
		xFECTxDescriptors[ ux ].status = TX_BD_TC;
		xFECTxDescriptors[ ux ].data = NULL;
		xFECTxDescriptors[ ux ].length = 0;
	}

	pcBufPointer = &( ucFECRxBuffers[ 0 ] );
	while( ( ( unsigned portLONG ) pcBufPointer & 0x0fUL ) != 0 )
	{
		pcBufPointer++;
	}

	for( ux = 0; ux < configNUM_FEC_RX_BUFFERS; ux++ )
	{
	    xFECRxDescriptors[ ux ].status = RX_BD_E;
	    xFECRxDescriptors[ ux ].length = configFEC_BUFFER_SIZE;
	    xFECRxDescriptors[ ux ].data = pcBufPointer;
	    pcBufPointer += configFEC_BUFFER_SIZE;
	}

	/* Set the wrap bit in the last descriptors to form a ring. */
	xFECTxDescriptors[ fecNUM_FEC_TX_BUFFERS - 1 ].status |= TX_BD_W;
	xFECRxDescriptors[ configNUM_FEC_RX_BUFFERS - 1 ].status |= RX_BD_W;

	uxNextRxBuffer = 0;
}
/*-----------------------------------------------------------*/

void vInitFEC( void )
{
unsigned portSHORT usData;
struct uip_eth_addr xAddr;
const unsigned portCHAR ucMACAddress[6] =
{
	configMAC_0, configMAC_1,configMAC_2, configMAC_3, configMAC_4, configMAC_5
};

	/* Create the semaphore used by the ISR to wake the uIP task. */
	vSemaphoreCreateBinary( xFECSemaphore );
	vSemaphoreCreateBinary( xTxSemaphore );

	prvInitialiseFECBuffers();

	for( usData = 0; usData < 6; usData++ )
	{
		xAddr.addr[ usData ] = ucMACAddress[ usData ];
	}
	uip_setethaddr( xAddr );

	/* Set the Reset bit and clear the Enable bit */
	MCF_FEC_ECR = MCF_FEC_ECR_RESET;

	/* Wait at least 8 clock cycles */
	for( usData = 0; usData < 10; usData++ )
	{
		asm( "NOP" );
	}

	/* Set MII speed to 2.5MHz. */
	MCF_FEC_MSCR = MCF_FEC_MSCR_MII_SPEED( ( ( ( configCPU_CLOCK_HZ / 1000000 ) / 5 ) + 1 ) );

	/* Initialize PLDPAR to enable Ethernet LEDs. */
	MCF_GPIO_PLDPAR =  MCF_GPIO_PLDPAR_ACTLED_ACTLED | MCF_GPIO_PLDPAR_LINKLED_LINKLED | MCF_GPIO_PLDPAR_SPDLED_SPDLED
					 | MCF_GPIO_PLDPAR_DUPLED_DUPLED | MCF_GPIO_PLDPAR_COLLED_COLLED | MCF_GPIO_PLDPAR_RXLED_RXLED
					 | MCF_GPIO_PLDPAR_TXLED_TXLED;

	/* Initialize Port TA to enable Axcel control. */
	MCF_GPIO_PTAPAR = 0x00;
	MCF_GPIO_DDRTA  = 0x0F;
	MCF_GPIO_PORTTA = 0x04;

	/* Set phy address to zero. */
	MCF_EPHY_EPHYCTL1 = MCF_EPHY_EPHYCTL1_PHYADD( 0 );

	/* Enable EPHY module with PHY clocks disabled.  Do not turn on PHY clocks
	until both FEC and EPHY are completely setup (see Below). */
	MCF_EPHY_EPHYCTL0 = (uint8)(MCF_EPHY_EPHYCTL0_DIS100 | MCF_EPHY_EPHYCTL0_DIS10);

	/* Enable auto_neg at start-up */
	MCF_EPHY_EPHYCTL0 = (uint8)(MCF_EPHY_EPHYCTL0 & (MCF_EPHY_EPHYCTL0_ANDIS));

	/* Enable EPHY module. */
	MCF_EPHY_EPHYCTL0 = (uint8)(MCF_EPHY_EPHYCTL0_EPHYEN | MCF_EPHY_EPHYCTL0);

	/* Let PHY PLLs be determined by PHY. */
	MCF_EPHY_EPHYCTL0 = (uint8)(MCF_EPHY_EPHYCTL0  & ~(MCF_EPHY_EPHYCTL0_DIS100 | MCF_EPHY_EPHYCTL0_DIS10));

	/* Settle. */
	vTaskDelay( fecLINK_DELAY );

	/* Can we talk to the PHY? */
	do
	{
		vTaskDelay( fecLINK_DELAY );
		usData = 0;
		fec_mii_read( configPHY_ADDRESS, PHY_PHYIDR1, &usData );

	} while( usData == 0xffff );

	do
	{
		/* Start auto negotiate. */
		fec_mii_write( configPHY_ADDRESS, PHY_BMCR, ( PHY_BMCR_AN_RESTART | PHY_BMCR_AN_ENABLE ) );

		/* Wait for auto negotiate to complete. */
		do
		{
			vTaskDelay( fecLINK_DELAY );
			fec_mii_read( configPHY_ADDRESS, PHY_BMSR, &usData );

		} while( !( usData & PHY_BMSR_AN_COMPLETE ) );

	} while( 0 ); //while( !( usData & PHY_BMSR_LINK ) );

	/* When we get here we have a link - find out what has been negotiated. */
	fec_mii_read( configPHY_ADDRESS, PHY_ANLPAR, &usData );

	if( ( usData & PHY_ANLPAR_100BTX_FDX ) || ( usData & PHY_ANLPAR_100BTX ) )
	{
		/* Speed is 100. */
	}
	else
	{
		/* Speed is 10. */
	}

	if( ( usData & PHY_ANLPAR_100BTX_FDX ) || ( usData & PHY_ANLPAR_10BTX_FDX ) )
	{
		MCF_FEC_RCR &= (unsigned portLONG)~MCF_FEC_RCR_DRT;
		MCF_FEC_TCR |= MCF_FEC_TCR_FDEN;
	}
	else
	{
		MCF_FEC_RCR |= MCF_FEC_RCR_DRT;
		MCF_FEC_TCR &= (unsigned portLONG)~MCF_FEC_TCR_FDEN;
	}

	/* Clear the Individual and Group Address Hash registers */
	MCF_FEC_IALR = 0;
	MCF_FEC_IAUR = 0;
	MCF_FEC_GALR = 0;
	MCF_FEC_GAUR = 0;

	/* Set the Physical Address for the selected FEC */
	fec_set_address( ucMACAddress );

	/* Set Rx Buffer Size */
	MCF_FEC_EMRBR = (unsigned portSHORT)configFEC_BUFFER_SIZE;

	/* Point to the start of the circular Rx buffer descriptor queue */
	MCF_FEC_ERDSR = ( volatile unsigned portLONG ) &( xFECRxDescriptors[ 0 ] );

	/* Point to the start of the circular Tx buffer descriptor queue */
	MCF_FEC_ETSDR = ( volatile unsigned portLONG ) &( xFECTxDescriptors[ 0 ] );

	/* Mask all FEC interrupts */
	MCF_FEC_EIMR = ( unsigned portLONG ) -1;

	/* Clear all FEC interrupt events */
	MCF_FEC_EIR = ( unsigned portLONG ) -1;

	/* Initialize the Receive Control Register */
	MCF_FEC_RCR = MCF_FEC_RCR_MAX_FL(ETH_MAX_FRM) | MCF_FEC_RCR_FCE;

	MCF_FEC_RCR |= MCF_FEC_RCR_MII_MODE;

	#if( configUSE_PROMISCUOUS_MODE == 1 )
	{
		MCF_FEC_RCR |= MCF_FEC_RCR_PROM;
	}
	#endif

	prvEnableFECInterrupts();

	MCF_FEC_ECR = MCF_FEC_ECR_ETHER_EN;
	MCF_FEC_RDAR = MCF_FEC_RDAR_R_DES_ACTIVE;
}
/*-----------------------------------------------------------*/

static void prvEnableFECInterrupts( void )
{
const unsigned portBASE_TYPE uxFirstFECVector = 23, uxLastFECVector = 35;
unsigned portBASE_TYPE ux;

#if configFEC_INTERRUPT_PRIORITY > configMAX_SYSCALL_INTERRUPT_PRIORITY
	#error configFEC_INTERRUPT_PRIORITY must be less than or equal to configMAX_SYSCALL_INTERRUPT_PRIORITY
#endif

	for( ux = uxFirstFECVector; ux <= uxLastFECVector; ux++ )
	{
		MCF_INTC0_ICR( ux ) = MCF_INTC_ICR_IL( configFEC_INTERRUPT_PRIORITY );
	}

	/* Enable the FEC interrupts in the mask register */
	MCF_INTC0_IMRH &= ~( MCF_INTC_IMRH_INT_MASK33 | MCF_INTC_IMRH_INT_MASK34 | MCF_INTC_IMRH_INT_MASK35 );
	MCF_INTC0_IMRL &= ~( MCF_INTC_IMRL_INT_MASK25 | MCF_INTC_IMRL_INT_MASK26 | MCF_INTC_IMRL_INT_MASK27
						| MCF_INTC_IMRL_INT_MASK28 | MCF_INTC_IMRL_INT_MASK29 | MCF_INTC_IMRL_INT_MASK30
						| MCF_INTC_IMRL_INT_MASK31 | MCF_INTC_IMRL_INT_MASK23 | MCF_INTC_IMRL_INT_MASK24
						| MCF_INTC_IMRL_MASKALL );

	/* Clear any pending FEC interrupt events */
	MCF_FEC_EIR = MCF_FEC_EIR_CLEAR_ALL;

	/* Unmask all FEC interrupts */
	MCF_FEC_EIMR = MCF_FEC_EIMR_UNMASK_ALL;
}
/*-----------------------------------------------------------*/

static void prvResetFEC( portBASE_TYPE xCalledFromISR )
{
portBASE_TYPE x;

	if( xCalledFromISR == pdFALSE )
	{
		taskENTER_CRITICAL();
	}

	{
		prvInitialiseFECBuffers();

		/* Set the Reset bit and clear the Enable bit */
		MCF_FEC_ECR = MCF_FEC_ECR_RESET;

		/* Wait at least 8 clock cycles */
		for( x = 0; x < 10; x++ )
		{
			asm( "NOP" );
		}

		MCF_FEC_ECR = MCF_FEC_ECR_ETHER_EN;
		MCF_FEC_RDAR = MCF_FEC_RDAR_R_DES_ACTIVE;
	}

	if( xCalledFromISR == pdFALSE )
	{
		taskEXIT_CRITICAL();
	}
}
/*-----------------------------------------------------------*/

unsigned short usGetFECRxData( void )
{
unsigned portSHORT usLen;

	/* Obtain the size of the packet and put it into the "len" variable. */
	usLen = xFECRxDescriptors[ uxNextRxBuffer ].length;

	if( ( usLen != 0 ) && ( ( xFECRxDescriptors[ uxNextRxBuffer ].status & RX_BD_E ) == 0 ) )
	{
		uip_buf = xFECRxDescriptors[ uxNextRxBuffer ].data;
	}
	else
	{
		usLen = 0;
	}

	return usLen;
}
/*-----------------------------------------------------------*/

void vDiscardRxData( void )
{
	/* Free the descriptor as the buffer it points to is no longer in use. */
	xFECRxDescriptors[ uxNextRxBuffer ].status |= RX_BD_E;
	MCF_FEC_RDAR = MCF_FEC_RDAR_R_DES_ACTIVE;
	uxNextRxBuffer++;
	if( uxNextRxBuffer >= configNUM_FEC_RX_BUFFERS )
	{
		uxNextRxBuffer = 0;
	}
}
/*-----------------------------------------------------------*/

void vSendBufferToFEC( void )
{
	/* Ensure no Tx frames are outstanding. */
	if( xSemaphoreTake( xTxSemaphore, fecTX_BUFFER_WAIT ) == pdPASS )
	{
		/* Get a DMA buffer into which we can write the data to send. */
		if( xFECTxDescriptors[ fecTX_BUFFER_TO_USE ].status & TX_BD_R )
		{
			/*** ERROR didn't expect this.  Sledge hammer error handling. ***/
			prvResetFEC( pdFALSE );

			/* Make sure we leave the semaphore in the expected state as nothing
			is being transmitted this will not happen in the Tx ISR. */
			xSemaphoreGive( xTxSemaphore );
		}
		else
		{
			/* Setup the buffer descriptor for transmission.  The data being
			sent is actually stored in one of the Rx descripter buffers,
			pointed to by uip_buf. */
			xFECTxDescriptors[ fecTX_BUFFER_TO_USE ].length = uip_len;
			xFECTxDescriptors[ fecTX_BUFFER_TO_USE ].status |= ( TX_BD_R | TX_BD_L );
			xFECTxDescriptors[ fecTX_BUFFER_TO_USE ].data = uip_buf;

			/* Remember which Rx descriptor owns the buffer we are sending. */
			uxIndexToBufferOwner = uxNextRxBuffer;

			/* We have finished with this Rx descriptor now. */
			uxNextRxBuffer++;
			if( uxNextRxBuffer >= configNUM_FEC_RX_BUFFERS )
			{
				uxNextRxBuffer = 0;
			}

			/* Continue the Tx DMA (in case it was waiting for a new TxBD) */
			MCF_FEC_TDAR = MCF_FEC_TDAR_X_DES_ACTIVE;
		}
	}
	else
	{
		vDiscardRxData();
	}
}
/*-----------------------------------------------------------*/

void vFEC_ISR( void )
{
unsigned portLONG ulEvent;
portBASE_TYPE xHighPriorityTaskWoken = pdFALSE;

	ulEvent = MCF_FEC_EIR & MCF_FEC_EIMR;
	MCF_FEC_EIR = ulEvent;

	if( ( ulEvent & MCF_FEC_EIR_RXB ) || ( ulEvent & MCF_FEC_EIR_RXF ) )
	{
		/* A packet has been received.  Wake the handler task. */
		xSemaphoreGiveFromISR( xFECSemaphore, &xHighPriorityTaskWoken );
	}

	if( ulEvent & ( MCF_FEC_EIR_UN | MCF_FEC_EIR_RL | MCF_FEC_EIR_LC | MCF_FEC_EIR_EBERR | MCF_FEC_EIR_BABT | MCF_FEC_EIR_BABR | MCF_FEC_EIR_HBERR ) )
	{
		/* Sledge hammer error handling. */
		prvResetFEC( pdTRUE );
	}

	if( ( ulEvent & MCF_FEC_EIR_TXF ) || ( ulEvent & MCF_FEC_EIR_TXB ) )
	{
		/* The buffer being sent is pointed to by an Rx descriptor, now the
		buffer has been sent we can mark the Rx descriptor as free again. */
		xFECRxDescriptors[ uxIndexToBufferOwner ].status |= RX_BD_E;
		MCF_FEC_RDAR = MCF_FEC_RDAR_R_DES_ACTIVE;
		xSemaphoreGiveFromISR( xTxSemaphore, &xHighPriorityTaskWoken );
	}

	portEND_SWITCHING_ISR( xHighPriorityTaskWoken );
}
/*-----------------------------------------------------------*/

void __attribute__ ((interrupt)) __cs3_isr_interrupt_87( void ) { vFEC_ISR(); }
void __attribute__ ((interrupt)) __cs3_isr_interrupt_88( void ) { vFEC_ISR(); }
void __attribute__ ((interrupt)) __cs3_isr_interrupt_89( void ) { vFEC_ISR(); }
void __attribute__ ((interrupt)) __cs3_isr_interrupt_90( void ) { vFEC_ISR(); }
void __attribute__ ((interrupt)) __cs3_isr_interrupt_91( void ) { vFEC_ISR(); }
void __attribute__ ((interrupt)) __cs3_isr_interrupt_92( void ) { vFEC_ISR(); }
void __attribute__ ((interrupt)) __cs3_isr_interrupt_93( void ) { vFEC_ISR(); }
void __attribute__ ((interrupt)) __cs3_isr_interrupt_94( void ) { vFEC_ISR(); }
void __attribute__ ((interrupt)) __cs3_isr_interrupt_95( void ) { vFEC_ISR(); }
void __attribute__ ((interrupt)) __cs3_isr_interrupt_96( void ) { vFEC_ISR(); }
void __attribute__ ((interrupt)) __cs3_isr_interrupt_97( void ) { vFEC_ISR(); }
void __attribute__ ((interrupt)) __cs3_isr_interrupt_98( void ) { vFEC_ISR(); }
void __attribute__ ((interrupt)) __cs3_isr_interrupt_99( void ) { vFEC_ISR(); }


