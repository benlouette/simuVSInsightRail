#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>

// Check if the compiler is MSVC
#ifdef _MSC_VER
    // Define a macro for MSVC
#define PACKED_STRUCT(name) \
        __pragma(pack(push, 1)) struct name __pragma(pack(pop))

// Check if the compiler is GCC or compatible
#elif defined(__GNUC__)
    // Define a macro for GCC
#define PACKED_STRUCT(name) struct __attribute__((packed)) name

#else
#error "Unsupported compiler"
#endif

// Simulated stack top
uint32_t simulated_stack_top;
uint32_t __RAM_VECTOR_TABLE_SIZE_BYTES = 0x400u; // bytes(or 1024 bytes)



// Simulated function prototypes (handlers)
void Reset_Handler(void);
void NMI_Handler(void);
void HardFault_Handler(void);
//void MemManage_Handler(void);
//void BusFault_Handler(void);
//void UsageFault_Handler(void);
//void SVC_Handler(void);
//void DebugMon_Handler(void);
//void PendSV_Handler(void);
//void SysTick_Handler(void);
//void DMA0_IRQHandler(void);
//void DMA1_IRQHandler(void);
//void DMA2_IRQHandler(void);
//void DMA3_IRQHandler(void);
//void DMA4_IRQHandler(void);
//void DMA5_IRQHandler(void);
//void DMA6_IRQHandler(void);
//void DMA7_IRQHandler(void);
//void DMA8_IRQHandler(void);
//void DMA9_IRQHandler(void);
//void DMA10_IRQHandler(void);
//void DMA11_IRQHandler(void);
//void DMA12_IRQHandler(void);
//void DMA13_IRQHandler(void);
//void DMA14_IRQHandler(void);
//void DMA15_IRQHandler(void);
//void DMA_Error_IRQHandler(void);
//void MCM_IRQHandler(void);
//void FTFE_IRQHandler(void);
//void Read_Collision_IRQHandler(void);
//void LVD_LVW_IRQHandler(void);
//void LLWU_IRQHandler(void);
//void WDOG_EWM_IRQHandler(void);
//void RNG_IRQHandler(void);
//void I2C0_IRQHandler(void);
//void I2C1_IRQHandler(void);
//void SPI0_IRQHandler(void);
//void SPI1_IRQHandler(void);
//void I2S0_Tx_IRQHandler(void);
//void I2S0_Rx_IRQHandler(void);
//void UART0_LON_IRQHandler(void);
//void UART0_RX_TX_IRQHandler(void);
//void UART0_ERR_IRQHandler(void);
//void UART1_RX_TX_IRQHandler(void);
//void UART1_ERR_IRQHandler(void);
//void UART2_RX_TX_IRQHandler(void);
//void UART2_ERR_IRQHandler(void);
//void UART3_RX_TX_IRQHandler(void);
//void UART3_ERR_IRQHandler(void);
//void ADC0_IRQHandler(void);
//void CMP0_IRQHandler(void);
//void CMP1_IRQHandler(void);
//void FTM0_IRQHandler(void);
//void FTM1_IRQHandler(void);
//void FTM2_IRQHandler(void);
//void CMT_IRQHandler(void);
//void RTC_IRQHandler(void);
//void RTC_Seconds_IRQHandler(void);
//void PIT0_IRQHandler(void);
//void PIT1_IRQHandler(void);
//void PIT2_IRQHandler(void);
//void PIT3_IRQHandler(void);
//void PDB0_IRQHandler(void);
//void USB0_IRQHandler(void);
//void USBDCD_IRQHandler(void);
//void Reserved71_IRQHandler(void);
//void DAC0_IRQHandler(void);
//void MCG_IRQHandler(void);
//void LPTMR0_IRQHandler(void);
//void PORTA_IRQHandler(void);
//void PORTB_IRQHandler(void);
//void PORTC_IRQHandler(void);
//void PORTD_IRQHandler(void);
//void PORTE_IRQHandler(void);
//void SWI_IRQHandler(void);
//void SPI2_IRQHandler(void);
//void UART4_RX_TX_IRQHandler(void);
//void UART4_ERR_IRQHandler(void);
//void UART5_RX_TX_IRQHandler(void);
//void UART5_ERR_IRQHandler(void);
//void CMP2_IRQHandler(void);
//void FTM3_IRQHandler(void);
//void DAC1_IRQHandler(void);
//void ADC1_IRQHandler(void);
//void I2C2_IRQHandler(void);
//void CAN0_ORed_Message_buffer_IRQHandler(void);
//void CAN0_Bus_Off_IRQHandler(void);
//void CAN0_Error_IRQHandler(void);
//void CAN0_Tx_Warning_IRQHandler(void);
//void CAN0_Rx_Warning_IRQHandler(void);
//void CAN0_Wake_Up_IRQHandler(void);
//void SDHC_IRQHandler(void);
//void Reserved98_IRQHandler(void);
//void Reserved99_IRQHandler(void);
//void Reserved100_IRQHandler(void);
//void Reserved101_IRQHandler(void);
void DefaultISR(void);



// Define the vector table as an array of function pointers
void (* const simulated_vector_table[])(void) = {
    (void (*)(void))(&simulated_stack_top),
Reset_Handler,                                   /* Reset Handler */
NMI_Handler             ,                                     /* NMI Handler*/
HardFault_Handler       ,                               /* Hard Fault Handler*/
DefaultISR       ,                               /* MPU Fault Handler*/
DefaultISR        ,                                /* Bus Fault Handler*/
DefaultISR      ,                              /* Usage Fault Handler*/
0,                                               /* Reserved*/
0,                                               /* Reserved*/
0,                                               /* Reserved*/
0,                                               /* Reserved*/
DefaultISR         ,                                     /* SVCall Handler*/
DefaultISR    ,                                /* Debug Monitor Handler*/
0                   ,                                               /* Reserved*/
DefaultISR      ,                                  /* PendSV Handler*/
DefaultISR     ,                                 /* SysTick Handler*/

/* External Interrupts*/
DefaultISR,    /* DMA Channel 0 Transfer Complete*/
DefaultISR,    /* DMA Channel 1 Transfer Complete*/
DefaultISR,    /* DMA Channel 2 Transfer Complete*/
DefaultISR,    /* DMA Channel 3 Transfer Complete*/
DefaultISR,    /* DMA Channel 4 Transfer Complete*/
DefaultISR,    /* DMA Channel 5 Transfer Complete*/
DefaultISR,    /* DMA Channel 6 Transfer Complete*/
DefaultISR,    /* DMA Channel 7 Transfer Complete*/
DefaultISR,    /* DMA Channel 8 Transfer Complete*/
DefaultISR,    /* DMA Channel 9 Transfer Complete*/
DefaultISR,    /* DMA Channel 10 Transfer Complete*/
DefaultISR,    /* DMA Channel 11 Transfer Complete*/
DefaultISR,    /* DMA Channel 12 Transfer Complete*/
DefaultISR,    /* DMA Channel 13 Transfer Complete*/
DefaultISR,    /* DMA Channel 14 Transfer Complete*/
DefaultISR,    /* DMA Channel 15 Transfer Complete*/
DefaultISR,    /* DMA Error Interrupt*/
DefaultISR,    /* Normal Interrupt*/
DefaultISR,    /* FTFE Command complete interrupt*/
DefaultISR,    /* Read Collision Interrupt*/
DefaultISR,    /* Low Voltage Detect, Low Voltage Warning*/
DefaultISR,    /* Low Leakage Wakeup Unit*/
DefaultISR,    /* WDOG Interrupt*/
DefaultISR,    /* RNG Interrupt*/
DefaultISR,    /* I2C0 interrupt*/
DefaultISR,    /* I2C1 interrupt*/
DefaultISR,    /* SPI0 Interrupt*/
DefaultISR,    /* SPI1 Interrupt*/
DefaultISR,    /* I2S0 transmit interrupt*/
DefaultISR,    /* I2S0 receive interrupt*/
DefaultISR,    /* UART0 LON interrupt*/
DefaultISR,    /* UART0 Receive/Transmit interrupt*/
DefaultISR,    /* UART0 Error interrupt*/
DefaultISR,    /* UART1 Receive/Transmit interrupt*/
DefaultISR,    /* UART1 Error interrupt*/
DefaultISR,    /* UART2 Receive/Transmit interrupt*/
DefaultISR,    /* UART2 Error interrupt*/
DefaultISR,    /* UART3 Receive/Transmit interrupt*/
DefaultISR,    /* UART3 Error interrupt*/
DefaultISR,    /* ADC0 interrupt*/
DefaultISR,    /* CMP0 interrupt*/
DefaultISR,    /* CMP1 interrupt*/
DefaultISR,    /* FTM0 fault, overflow and channels interrupt*/
DefaultISR,    /* FTM1 fault, overflow and channels interrupt*/
DefaultISR,    /* FTM2 fault, overflow and channels interrupt*/
DefaultISR,    /* CMT interrupt*/
DefaultISR,    /* RTC interrupt*/
DefaultISR,    /* RTC seconds interrupt*/
DefaultISR,    /* PIT timer channel 0 interrupt*/
DefaultISR,    /* PIT timer channel 1 interrupt*/
DefaultISR,    /* PIT timer channel 2 interrupt*/
DefaultISR,    /* PIT timer channel 3 interrupt*/
DefaultISR,    /* PDB0 Interrupt*/
DefaultISR,    /* USB0 interrupt*/
DefaultISR,    /* USBDCD Interrupt*/
DefaultISR,    /* Reserved interrupt 71*/
DefaultISR,    /* DAC0 interrupt*/
DefaultISR,    /* MCG Interrupt*/
DefaultISR,    /* LPTimer interrupt*/
DefaultISR,    /* Port A interrupt*/
DefaultISR,    /* Port B interrupt*/
DefaultISR,    /* Port C interrupt*/
DefaultISR,    /* Port D interrupt*/
DefaultISR,    /* Port E interrupt*/
DefaultISR,    /* Software interrupt*/
DefaultISR,    /* SPI2 Interrupt*/
DefaultISR,    /* UART4 Receive/Transmit interrupt*/
DefaultISR,    /* UART4 Error interrupt*/
DefaultISR,    /* UART5 Receive/Transmit interrupt*/
DefaultISR,    /* UART5 Error interrupt*/
DefaultISR,    /* CMP2 interrupt*/
DefaultISR,    /* FTM3 fault, overflow and channels interrupt*/
DefaultISR,    /* DAC1 interrupt*/
DefaultISR,    /* ADC1 interrupt*/
DefaultISR,    /* I2C2 interrupt*/
DefaultISR,    /* CAN0 OR'd message buffers interrupt*/
DefaultISR,    /* CAN0 bus off interrupt*/
DefaultISR,    /* CAN0 error interrupt*/
DefaultISR,    /* CAN0 Tx warning interrupt*/
DefaultISR,    /* CAN0 Rx warning interrupt*/
DefaultISR,    /* CAN0 wake up interrupt*/
DefaultISR,    /* SDHC interrupt*/
DefaultISR,    /* Reserved interrupt 98*/
DefaultISR,    /* Reserved interrupt 99*/
DefaultISR,    /* Reserved interrupt 100*/
DefaultISR,    /* Reserved interrupt 101*/
DefaultISR                                  ,    /* 102*/
DefaultISR                                  ,    /* 103*/
DefaultISR                                  ,    /* 104*/
DefaultISR                                  ,    /* 105*/
DefaultISR                                  ,    /* 106*/
DefaultISR                                  ,    /* 107*/
DefaultISR                                  ,    /* 108*/
DefaultISR                                  ,    /* 109*/
DefaultISR                                  ,    /* 110*/
DefaultISR                                  ,    /* 111*/
DefaultISR                                  ,    /* 112*/
DefaultISR                                  ,    /* 113*/
DefaultISR                                  ,    /* 114*/
DefaultISR                                  ,    /* 115*/
DefaultISR                                  ,    /* 116*/
DefaultISR                                  ,    /* 117*/
DefaultISR                                  ,    /* 118*/
DefaultISR                                  ,    /* 119*/
DefaultISR                                  ,    /* 120*/
DefaultISR                                  ,    /* 121*/
DefaultISR                                  ,    /* 122*/
DefaultISR                                  ,    /* 123*/
DefaultISR                                  ,    /* 124*/
DefaultISR                                  ,    /* 125*/
DefaultISR                                  ,    /* 126*/
DefaultISR                                  ,    /* 127*/
DefaultISR                                  ,    /* 128*/
DefaultISR                                  ,    /* 129*/
DefaultISR                                  ,    /* 130*/
DefaultISR                                  ,    /* 131*/
DefaultISR                                  ,    /* 132*/
DefaultISR                                  ,    /* 133*/
DefaultISR                                  ,    /* 134*/
DefaultISR                                  ,    /* 135*/
DefaultISR                                  ,    /* 136*/
DefaultISR                                  ,    /* 137*/
DefaultISR                                  ,    /* 138*/
DefaultISR                                  ,    /* 139*/
DefaultISR                                  ,    /* 140*/
DefaultISR                                  ,    /* 141*/
DefaultISR                                  ,    /* 142*/
DefaultISR                                  ,    /* 143*/
DefaultISR                                  ,    /* 144*/
DefaultISR                                  ,    /* 145*/
DefaultISR                                  ,    /* 146*/
DefaultISR                                  ,    /* 147*/
DefaultISR                                  ,    /* 148*/
DefaultISR                                  ,    /* 149*/
DefaultISR                                  ,    /* 150*/
DefaultISR                                  ,    /* 151*/
DefaultISR                                  ,    /* 152*/
DefaultISR                                  ,    /* 153*/
DefaultISR                                  ,    /* 154*/
DefaultISR                                  ,    /* 155*/
DefaultISR                                  ,    /* 156*/
DefaultISR                                  ,    /* 157*/
DefaultISR                                  ,    /* 158*/
DefaultISR                                  ,    /* 159*/
DefaultISR                                  ,    /* 160*/
DefaultISR                                  ,    /* 161*/
DefaultISR                                  ,    /* 162*/
DefaultISR                                  ,    /* 163*/
DefaultISR                                  ,    /* 164*/
DefaultISR                                  ,    /* 165*/
DefaultISR                                  ,    /* 166*/
DefaultISR                                  ,    /* 167*/
DefaultISR                                  ,    /* 168*/
DefaultISR                                  ,    /* 169*/
DefaultISR                                  ,    /* 170*/
DefaultISR                                  ,    /* 171*/
DefaultISR                                  ,    /* 172*/
DefaultISR                                  ,    /* 173*/
DefaultISR                                  ,    /* 174*/
DefaultISR                                  ,    /* 175*/
DefaultISR                                  ,    /* 176*/
DefaultISR                                  ,    /* 177*/
DefaultISR                                  ,    /* 178*/
DefaultISR                                  ,    /* 179*/
DefaultISR                                  ,    /* 180*/
DefaultISR                                  ,    /* 181*/
DefaultISR                                  ,    /* 182*/
DefaultISR                                  ,    /* 183*/
DefaultISR                                  ,    /* 184*/
DefaultISR                                  ,    /* 185*/
DefaultISR                                  ,    /* 186*/
DefaultISR                                  ,    /* 187*/
DefaultISR                                  ,    /* 188*/
DefaultISR                                  ,    /* 189*/
DefaultISR                                  ,    /* 190*/
DefaultISR                                  ,    /* 191*/
DefaultISR                                  ,    /* 192*/
DefaultISR                                  ,    /* 193*/
DefaultISR                                  ,    /* 194*/
DefaultISR                                  ,    /* 195*/
DefaultISR                                  ,    /* 196*/
DefaultISR                                  ,    /* 197*/
DefaultISR                                  ,    /* 198*/
DefaultISR                                  ,    /* 199*/
DefaultISR                                  ,    /* 200*/
DefaultISR                                  ,    /* 201*/
DefaultISR                                  ,    /* 202*/
DefaultISR                                  ,    /* 203*/
DefaultISR                                  ,    /* 204*/
DefaultISR                                  ,    /* 205*/
DefaultISR                                  ,    /* 206*/
DefaultISR                                  ,    /* 207*/
DefaultISR                                  ,    /* 208*/
DefaultISR                                  ,    /* 209*/
DefaultISR                                  ,    /* 210*/
DefaultISR                                  ,    /* 211*/
DefaultISR                                  ,    /* 212*/
DefaultISR                                  ,    /* 213*/
DefaultISR                                  ,    /* 214*/
DefaultISR                                  ,    /* 215*/
DefaultISR                                  ,    /* 216*/
DefaultISR                                  ,    /* 217*/
DefaultISR                                  ,    /* 218*/
DefaultISR                                  ,    /* 219*/
DefaultISR                                  ,    /* 220*/
DefaultISR                                  ,    /* 221*/
DefaultISR                                  ,    /* 222*/
DefaultISR                                  ,    /* 223*/
DefaultISR                                  ,    /* 224*/
DefaultISR                                  ,    /* 225*/
DefaultISR                                  ,    /* 226*/
DefaultISR                                  ,    /* 227*/
DefaultISR                                  ,    /* 228*/
DefaultISR                                  ,    /* 229*/
DefaultISR                                  ,    /* 230*/
DefaultISR                                  ,    /* 231*/
DefaultISR                                  ,    /* 232*/
DefaultISR                                  ,    /* 233*/
DefaultISR                                  ,    /* 234*/
DefaultISR                                  ,    /* 235*/
DefaultISR                                  ,    /* 236*/
DefaultISR                                  ,    /* 237*/
DefaultISR                                  ,    /* 238*/
DefaultISR                                  ,    /* 239*/
DefaultISR                                  ,    /* 240*/
DefaultISR                                  ,    /* 241*/
DefaultISR                                  ,    /* 242*/
DefaultISR                                  ,    /* 243*/
DefaultISR                                  ,    /* 244*/
DefaultISR                                  ,    /* 245*/
DefaultISR                                  ,    /* 246*/
DefaultISR                                  ,    /* 247*/
DefaultISR                                  ,    /* 248*/
DefaultISR                                  ,    /* 249*/
DefaultISR                                  ,    /* 250*/
DefaultISR                                  ,    /* 251*/
DefaultISR                                  ,    /* 252*/
DefaultISR                                  ,    /* 253*/
DefaultISR                                  ,    /* 254*/
};

// Define the sizes of the memory regions based on the linker script
#define VECTOR_RAM_SIZE 0x400u
#define DATA_ROM_SIZE   0x7A400
#define DATA_RAM_SIZE   0x17C00
#define BSS_SIZE        0x18000  // Adjust this size according to your needs
// Declare the variables representing memory regions
uint32_t __VECTOR_RAM[VECTOR_RAM_SIZE / sizeof(uint32_t)];
uint32_t __DATA_ROM[DATA_ROM_SIZE / sizeof(uint32_t)];
uint32_t __DATA_RAM[DATA_RAM_SIZE / sizeof(uint32_t)];
char __DATA_END[DATA_ROM_SIZE];
char __START_BSS[BSS_SIZE];
char __END_BSS[BSS_SIZE];



// Example implementation of Reset_Handler
void Reset_Handler(void) {
    // Initialization code
    printf("System Initialization\n");
    // Jump to main or other start-up routines
}
// Default Interrupt Service Routine
void DefaultISR(void) {
    printf("Unhandled interrupt\n");
    while (1) {} // Infinite loop
}
void NMI_Handler(void) {
    printf("NMI_Handler\n");
}
//void HardFault_Handler(void) {
//    printf("HardFault_Handler   \n");
//}
//void MemManage_Handler(void) {
//    printf(" MemManage_Handler  \n");
//}
//void BusFault_Handler(void) {
//    printf(" BusFault_Handler  \n");
//}
//void UsageFault_Handler(void) {
//    printf("UsageFault_Handler   \n");
//}
//void SVC_Handler(void) {
//    printf("SVC_Handler   \n");
//}
//void DebugMon_Handler(void) {
//    printf("DebugMon_Handler   \n");
//}
//void PendSV_Handler(void) {
//    printf(" PendSV_Handler  \n");
//}
//void SysTick_Handler(void) {
//    printf("SysTick_Handler   \n");
//}
//void DMA0_IRQHandler(void) {
//    printf(" DMA0_IRQHandler  \n");
//}
//void DMA1_IRQHandler(void) {
//    printf("DMA1_IRQHandler   \n");
//}
//void DMA2_IRQHandler(void) {
//    printf(" DMA2_IRQHandler  \n");
//}
//void DMA3_IRQHandler(void) {
//    printf("DMA3_IRQHandler   \n");
//}
//void DMA4_IRQHandler(void) {
//    printf(" DMA4_IRQHandler  \n");
//}
//void DMA5_IRQHandler(void) {
//    printf(" DMA5_IRQHandler  \n");
//}
//void DMA6_IRQHandler(void) {
//    printf("DMA6_IRQHandler   \n");
//}
//void DMA7_IRQHandler(void) {
//    printf("DMA7_IRQHandler   \n");
//}
//void DMA8_IRQHandler(void) {
//    printf("DMA8_IRQHandler   \n");
//}
//void DMA9_IRQHandler(void) {
//    printf("DMA9_IRQHandler   \n");
//}
//void DMA10_IRQHandler(void) {
//    printf("DMA10_IRQHandler   \n");
//}
//void DMA11_IRQHandler(void) {
//    printf("DMA11_IRQHandler   \n");
//}
//void DMA12_IRQHandler(void) {
//    printf("DMA12_IRQHandler   \n");
//}
//void DMA13_IRQHandler(void) {
//    printf("DMA13_IRQHandler   \n");
//}
//void DMA14_IRQHandler(void) {
//    printf("DMA14_IRQHandler   \n");
//}
//void DMA15_IRQHandler(void) {
//    printf(" DMA15_IRQHandler  \n");
//}
//void DMA_Error_IRQHandler(void) {
//    printf(" DMA_Error_IRQHandler  \n");
//}
//void MCM_IRQHandler(void) {
//    printf("MCM_IRQHandler   \n");
//}
//void FTFE_IRQHandler(void) {
//    printf(" FTFE_IRQHandler  \n");
//}
//void Read_Collision_IRQHandler(void) {
//    printf("Read_Collision_IRQHandler   \n");
//}
//void LVD_LVW_IRQHandler(void) {
//    printf("LVD_LVW_IRQHandler   \n");
//}
//void LLWU_IRQHandler(void) {
//    printf("LLWU_IRQHandler   \n");
//}
//void WDOG_EWM_IRQHandler(void) {
//    printf("WDOG_EWM_IRQHandler   \n");
//}
//void RNG_IRQHandler(void) {
//    printf("RNG_IRQHandler   \n");
//}
//void I2C0_IRQHandler(void) {
//    printf(" I2C0_IRQHandler  \n");
//}
//void I2C1_IRQHandler(void) {
//    printf("I2C1_IRQHandler   \n");
//}
////void SPI0_IRQHandler(void) {
////    printf("SPI0_IRQHandler  \n");
////}
////void SPI1_IRQHandler(void) {
////    printf(" SPI1_IRQHandler  \n");
////}
//void I2S0_Tx_IRQHandler(void) {
//    printf("I2S0_Tx_IRQHandler   \n");
//}
//void I2S0_Rx_IRQHandler(void) {
//    printf(" I2S0_Rx_IRQHandler  \n");
//}
//void UART0_LON_IRQHandler(void) {
//    printf(" UART0_LON_IRQHandler  \n");
//}
//void UART0_RX_TX_IRQHandler(void) {
//    printf(" UART0_RX_TX_IRQHandler  \n");
//}
//void UART0_ERR_IRQHandler(void) {
//    printf(" UART0_ERR_IRQHandler  \n");
//}
//void UART1_RX_TX_IRQHandler(void) {
//    printf("UART1_RX_TX_IRQHandler   \n");
//}
//void UART1_ERR_IRQHandler(void) {
//    printf("UART1_ERR_IRQHandler   \n");
//}
//void UART2_RX_TX_IRQHandler(void) {
//    printf(" UART2_RX_TX_IRQHandler  \n");
//}
//void UART2_ERR_IRQHandler(void) {
//    printf(" UART2_ERR_IRQHandler  \n");
//}
//void UART3_RX_TX_IRQHandler(void) {
//    printf(" UART3_RX_TX_IRQHandler  \n");
//}
//void UART3_ERR_IRQHandler(void) {
//    printf("UART3_ERR_IRQHandler   \n");
//}
//void ADC0_IRQHandler(void) {
//    printf("ADC0_IRQHandler   \n");
//}
//void CMP0_IRQHandler(void) {
//    printf(" CMP0_IRQHandler  \n");
//}
//void CMP1_IRQHandler(void) {
//    printf("CMP1_IRQHandler   \n");
//}
//void FTM0_IRQHandler(void) {
//    printf("FTM0_IRQHandler   \n");
//}
//void FTM1_IRQHandler(void) {
//    printf("FTM1_IRQHandler   \n");
//}
//void FTM2_IRQHandler(void) {
//    printf(" FTM2_IRQHandler  \n");
//}
//void CMT_IRQHandler(void) {
//    printf(" CMT_IRQHandler  \n");
//}
//void RTC_IRQHandler(void) {
//    printf("RTC_IRQHandler   \n");
//}
//void RTC_Seconds_IRQHandler(void) {
//    printf(" RTC_Seconds_IRQHandler  \n");
//}
//void PIT0_IRQHandler(void) {
//    printf(" PIT0_IRQHandler  \n");
//}
//void PIT1_IRQHandler(void) {
//    printf(" PIT1_IRQHandler  \n");
//}
//void PIT2_IRQHandler(void) {
//    printf(" PIT2_IRQHandler  \n");
//}
//void PIT3_IRQHandler(void) {
//    printf("PIT3_IRQHandler   \n");
//}
//void PDB0_IRQHandler(void) {
//    printf("PDB0_IRQHandler   \n");
//}
//void USB0_IRQHandler(void) {
//    printf(" USB0_IRQHandler  \n");
//}
//void USBDCD_IRQHandler(void) {
//    printf(" USBDCD_IRQHandler  \n");
//}
//void Reserved71_IRQHandler(void) {
//    printf(" Reserved71_IRQHandler  \n");
//}
//void DAC0_IRQHandler(void) {
//    printf("DAC0_IRQHandler   \n");
//}
//void MCG_IRQHandler(void) {
//    printf("MCG_IRQHandler   \n");
//}
//void LPTMR0_IRQHandler(void) {
//    printf("LPTMR0_IRQHandler   \n");
//}
//void PORTA_IRQHandler(void) {
//    printf(" PORTA_IRQHandler  \n");
//}
//void PORTB_IRQHandler(void) {
//    printf("PORTB_IRQHandler  \n");
//}
//void PORTC_IRQHandler(void) {
//    printf(" PORTC_IRQHandler  \n");
//}
//void PORTD_IRQHandler(void) {
//    printf(" PORTD_IRQHandler  \n");
//}
//void PORTE_IRQHandler(void) {
//    printf("  PORTE_IRQHandler \n");
//}
//void SWI_IRQHandler(void) {
//    printf("SWI_IRQHandler   \n");
//}
////void SPI2_IRQHandler(void) {
////    printf("SPI2_IRQHandler   \n");
////}
//void UART4_RX_TX_IRQHandler(void) {
//    printf(" UART4_RX_TX_IRQHandler  \n");
//}
//void UART4_ERR_IRQHandler(void) {
//    printf("UART4_ERR_IRQHandler   \n");
//}
//void UART5_RX_TX_IRQHandler(void) {
//    printf("UART5_RX_TX_IRQHandler   \n");
//}
//void UART5_ERR_IRQHandler(void) {
//    printf("UART5_ERR_IRQHandler   \n");
//}
//void CMP2_IRQHandler(void) {
//    printf(" CMP2_IRQHandler  \n");
//}
//void FTM3_IRQHandler(void) {
//    printf(" FTM3_IRQHandler  \n");
//}
//void DAC1_IRQHandler(void) {
//    printf(" DAC1_IRQHandler  \n");
//}
//void ADC1_IRQHandler(void) {
//    printf(" ADC1_IRQHandler  \n");
//}
//void I2C2_IRQHandler(void) {
//    printf(" I2C2_IRQHandler  \n");
//}
//void CAN0_ORed_Message_buffer_IRQHandler(void) {
//    printf(" CAN0_ORed_Message_buffer_IRQHandler  \n");
//}
//void CAN0_Bus_Off_IRQHandler(void) {
//    printf(" CAN0_Bus_Off_IRQHandler  \n");
//}
//void CAN0_Error_IRQHandler(void) {
//    printf("  CAN0_Error_IRQHandler \n");
//}
//void CAN0_Tx_Warning_IRQHandler(void) {
//    printf("CAN0_Tx_Warning_IRQHandler   \n");
//}
//void CAN0_Rx_Warning_IRQHandler(void) {
//    printf(" CAN0_Rx_Warning_IRQHandler  \n");
//}
//void CAN0_Wake_Up_IRQHandler(void) {
//    printf(" CAN0_Wake_Up_IRQHandler  \n");
//}
//void SDHC_IRQHandler(void) {
//    printf(" SDHC_IRQHandler  \n");
//}
//void Reserved98_IRQHandler(void) {
//    printf(" Reserved98_IRQHandler  \n");
//}
//void Reserved99_IRQHandler(void) {
//    printf(" Reserved99_IRQHandler  \n");
//}
//void Reserved100_IRQHandler(void) {
//    printf(" Reserved100_IRQHandler  \n");
//}
//void Reserved101_IRQHandler(void) {
//    printf("Reserved101_IRQHandler  \n");
//}
////void DefaultISR(void) {
//    printf(" DefaultISR  \n");
//}




#ifdef __cplusplus
}
#endif


















































































































































