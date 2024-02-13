#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "binaryCLI.h"

//
//
// my own printf routine, 
// did not find a simple version without some kind of license,
// so instead of putting time in understanding what can/cannot be done with the licensed code,
// I hacked my own version in probably less time.
//
//
// different sizes of int/long etc, can be a real headache, beware of this for each different architecture/compiler
//

#define PROTECT_PRINTF

/*
 * TEST define used for verification of printgdf against standard printf output on general purpose machine
 */
//#define TEST

#ifndef TEST

// the routine to dump the character (to a serial port or whatever you like)
//#define put_ch(a) putchar(a)
//void put_ch(uint8_t c);
//void put_ch_nb(uint8_t c);
//#define PUT_CH_INT(m_blockvar,m_charvar)    do { if ((m_blockvar)) put_ch(m_charvar); else put_ch_nb(m_charvar);} while(0);

#else

// just some code for  testing against the 'real' printf

#include <string.h>
// some testcode 
#define put_ch(a) buf_ch(a)

static int test_idx=0;
static char test_buf[512];
static void init_test_buf()
{

	memset(test_buf,0,sizeof(test_buf));
	test_idx=0;
	}
static void buf_ch(char a)
{
	if (test_idx <sizeof(test_buf)-1) {
		test_buf[test_idx++]=a;
	}
}

#endif

#define FLAG_PRECISION (1)
#define FLAG_ZEROS (2)
#define FLAG_LONG (4)
#define FLAG_LONGLONG (8)

#ifdef PROTECT_PRINTF
	static SemaphoreHandle_t xSemaphore = NULL;
#endif

//Lock printf to ensure CLI task is not printing before suspending
void suspendCli()
{
	extern TaskHandle_t   _TaskHandle_CLI;

	if(!xSemaphore)
	{
		// on the battery variant, the semaphore may not be valid at this point
		xSemaphore = xSemaphoreCreateMutex();
    }
	if(true == xSemaphoreTake(xSemaphore, 10000))
	{
		vTaskSuspend(_TaskHandle_CLI);
		xSemaphoreGive(xSemaphore);
	}
}

//
// needed for %lld, %llu, %llx (64 bits integers) on 32 bit architecture
// this issignificant slower, another reason to give it its own separate routine, and call it only when needed
//
// TODO test this more extensively
//
static void print64on32(void (*put_c)(uint8_t c), char fc, char flags, short width, short precision, unsigned long long int val)
{

      char buf[25];
      char filler = ' ';
      short i=0;


      if (flags & FLAG_ZEROS) filler = '0';
      if (fc=='X' || fc=='x') {
          char Case = (fc=='X' ? 'A' : 'a')-10;

          do {
              int c;
              c = val & 0xf;
              buf[i++] = c > 9 ? c + Case : c + '0';
              val >>= 4;
          } while (val);
      } else {
          short sgn = fc=='d';
          if (sgn) {
              if ( (long long)val <0) {
                  val = ~val+1;
              } else {
                  sgn=0;
              }
          }

          do {
              buf[i++] = val % 10 + '0';
              val = val / 10;
          } while (val);

	 if (flags & FLAG_PRECISION) {
	 	// leading zero's to get the decimal point at the right place
	 	while (i<(precision+1)) {
	 		buf[i++]='0';
		} 
	 }
         if (sgn) buf[i++]= '-';
      }

      width -= i;
      if ((flags & FLAG_PRECISION) && (precision != 0)) width--; // make room for the decimal point 
      while(width-- > 0) put_c( filler);
      while(i--) {
		put_c(buf[i]);
      		if ((flags & FLAG_PRECISION) && (i==precision)) put_c('.');
      }
}


static void print_double(void (*put_c)(uint8_t c), char fc, char flags, short width, short precision, double dval)
{
	int i;
	union {
		double d;
		int16_t i16[4];
	} tmp;

	tmp.d=dval;
	
	if ((flags & FLAG_PRECISION)==0) {
		precision = 6;
	}

	
	if (width && (precision  >= width)) {
	    precision = width-1;
	}
	for (i=0; i<precision; i++) {
		tmp.d *= 10;
	}
	if ( (fc=='f' || fc=='F') && (tmp.d < 1.0e23 && tmp.d > -1.0e23) ) {
	    print64on32(put_c, 'd', FLAG_PRECISION , width, precision, (long long) llrintf  (tmp.d));
	} else {
		// really stupid implementation
		int exp10=0;
		int sgn=1;

		int maxexp;// we do not handle 'NaN' yet, this sets a limit.
		if (sizeof(double) == 4) {
		    maxexp=24;
		} else {
		    maxexp=310;// 8 byte ieee double
		}
		tmp.d=dval;
#if 0
		{		
			int exp2;
			// printf("%04x %04x %04x %04x\n", (uint16_t) tmp.i16[0], (uint16_t) tmp.i16[1], (uint16_t) tmp.i16[2], (uint16_t) tmp.i16[3]);
			// exp2= ((tmp.i16[0]>>4) & 0x7ff)-1023;
			// printf("exp2=%d\n",exp2);
			exp2= ((tmp.i16[3]>>4) & 0x7ff)-1023;
			printf("exp2=%d\n",exp2);
		}
#endif		
		if (tmp.d!=0) {
			if (tmp.d<0) {
				sgn=-1;
				tmp.d=-tmp.d;
			}
			while (tmp.d>=10.0 && (exp10 < maxexp)) {
				tmp.d/=10;
				exp10++;
			}
			while (tmp.d<1.0 && (exp10 > -maxexp)) {
				tmp.d*=10;
				exp10--;
			}
		}
		if (sgn==-1) {
			tmp.d=-tmp.d;
		}
		for (i=0; i<precision; i++) {
			tmp.d *= 10;
		}
	        print64on32(put_c, 'd', FLAG_PRECISION , width, precision, (long long) llrintf (tmp.d));
	        if (fc=='E' || fc=='F' || fc=='G') {
	        	put_c('E');
	        } else {
	        	put_c('e');
	        }
	        if (exp10>=0) {
	        	put_c('+');
	        }
	        print64on32(put_c, 'd', 0 , 0, 0, (long long) llrintf(exp10));

	}
}
/*
 'my' printf version : printgdf()
 supports :
 	\n converts to \r\n
	%%
	%c
	%s
	%<width><l><l>d
 	%0<width><l><l>d
	%<width><l><l>u
	%<width><l><l>x		
	%0<width><l><l>x
	%<width><l><l>X		
	%0<width><l><l>X

	not full/fool proof parsing, so silly format strings will probably give silly results
	
 */
void printgdf(void (*put_c)(uint8_t c), const char *format, ...)
{
	char c;
	va_list args;

	va_start(args, format);

#ifdef PROTECT_PRINTF
	if(!xSemaphore)
	{
		xSemaphore = xSemaphoreCreateMutex();
    }
	if(true == xSemaphoreTake( xSemaphore, 200 ))
	{
#endif
	
	while( (c = *format++) != 0) {
		if (c == '%') {
			short width=0;
			short precision=0;
			char flags=0; // zero filled etc.
			char done=0;


			char fc=*format;

			// read possible width specification
			while (fc>='0' && fc <='9') {
				fc -= '0';
				if (fc==0 && width==0) flags |=FLAG_ZEROS;
				width = width*10+fc;					
				fc=*++format;
			}

			// read possible precision specification
			if (fc == '.') {
				fc=*++format;
				flags |= FLAG_PRECISION;
				while (fc>='0' && fc <='9') {
					fc -= '0';
					precision =precision*10+fc;					
					fc=*++format;
				}
			}
			
			do {
				fc=*format++;		
				
				switch(fc) {

				case '%':
				    put_c( fc);
					done=1;
					break;
				case 'c':
				    put_c( va_arg(args, int));
					done=1;
					break;
				case 'l':
					if (flags & FLAG_LONG) flags |= FLAG_LONGLONG; // %ll
					flags |= FLAG_LONG; // %l						
					break;
				case 's':      // zero terminated string
					{
					char * p;
	
						p= va_arg(args, char*);
						while (*p) {
						    put_c( *p++);
						}
						done=1;
					}
					break;	

				case 'f':
				case 'e':
				case 'g':
				case 'E':
				case 'G':
					{
						double dval;
						dval = va_arg(args, double);

						print_double(put_c, fc, flags, width, precision, dval);
						done = 1;
					}
					break;
				case 'd':
				case 'u':
				case 'x':
				case 'X':
					{

						if (flags & FLAG_LONGLONG) {
						    unsigned long long int val64;
						    // 64 bit value, no fun on 32 bit cpu, arithmatic will be much slower
						    // handle this one in a separate routine
						    val64 = va_arg(args, long long int);
						    print64on32(put_c, fc, flags & ~FLAG_PRECISION, width, precision, val64);
						    done = 1;
						} else {
	                            unsigned long val;
#if __WORDSIZE == 64
	                            char buf[25]; // can be 12 if long is 32 bit
#else
	                            char buf[12]; // can be 12 if long is 32 bit
#endif
	                            char filler = ' ';
	                            short i=0;

                                if (flags & FLAG_LONG) {
                                    val = va_arg(args, long);
                                } else {
                                    val = va_arg(args, int);
                                    // we assumed a signed int and put it in a long, in the case it was unsigned, we must get rid of the sign extension
                                    // only needed when long is larger than int, when the same, this does nothing usefull or harmfull
                                    if ( fc!='d') val &= (unsigned int) ( (int) -1);
                                }
                                if (flags & FLAG_ZEROS) filler = '0';
                                if (fc=='X' || fc=='x') {
                                    char Case = (fc=='X' ? 'A' : 'a')-10;

                                    do {
                                        int c;
                                        c = val & 0xf;
                                        buf[i++] = c > 9 ? c + Case : c + '0';
                                        val >>= 4;
                                    } while (val);
                                } else {
                                    short sgn = fc=='d';
                                    if (sgn) {
                                        if ( (long)val <0) {
                                            val = ~val+1;
                                        } else {
                                            sgn=0;
                                        }
                                    }

                                    do {
                                        buf[i++] = val % 10 + '0';
                                        val = val / 10;
                                    } while (val);

                                    if (sgn) buf[i++]= '-';
                                }

                                width -= i;
                                while(width-- > 0) put_c( filler);
                                while(i--) put_c(buf[i]);
                                done=1;
							}
						}
						break;

				default:
						format--;// we bumped at something not supported
						done=1;
					break;
				}
				
			} while (!done);

		} else { // not %
#ifndef TEST
			if (c=='\n') put_c( '\r'); // convert newlines to carriage return + newline
#endif
			put_c( c);
		}
	
	} //while

	if(binaryCLI_getMode() == E_CLI_MODE_BINARY)
	{
		binaryCLI_sendASCIIbuffer();
	}

#ifdef PROTECT_PRINTF
		xSemaphoreGive( xSemaphore );
	}
#endif

	va_end(args);
}



#ifdef TEST

int  main(void)
{
#if 0

	long i;
	char a='a';
	unsigned int u = 0x8000;
	int  d =  0x7fff;
	unsigned long lu=0x80000000;
	long ld =0x7fffffff;
	
	for (i=0; i<0x10000000;i=i*10+1) {
		uint_to_dec(i,(8<<2) | 1);
		printf(" = %8ld  ",i);
		uint_to_dec(-i,(8<<2) | 1);
		printf(" = %8ld\r\n",-i);

		uint_to_hex(i,(8<<2) | 0);
		putchar(' ');
		uint_to_hex(i,(8<<2) | 2);

		putchar(' ');
		uint_to_hex(i,(8<<2) | 1);

		printf(" = %8lx %08lx %8lX\r\n",i,i,i);

		printf("\r\n");
	}


	printgdf("ladila\r\n");
	printgdf(" -> #%s#  %% %c\r\n","string",'C');
	printgdf(" %d %u %x %ld %lu %lx\r\n",d,u,u,ld,lu,lu);
	   printf(" %d %u %x %ld %lu %lx\r\n",d,u,u,ld,lu,lu);
	
#endif

#if 1

 	printf("%f\n",-12345.6789);
	init_test_buf();
	printgdf(buf_ch,"%f\n\n",-12345.6789);
	puts(test_buf);
	
 	printf("%6.3f\n",-12345.6789);
	init_test_buf();
	printgdf(buf_ch,"%6.3f\n\n",-12345.6789);
	puts(test_buf);

	printf("%f\n",-0.06789);
	init_test_buf();
	printgdf(buf_ch,"%f\n\n",-0.06789);
	puts(test_buf);

	printf("%e\n",123e25);
	init_test_buf();
	printgdf(buf_ch,"%e\n\n",123e25);
	puts(test_buf);

	printf("%e\n",-123e25);
	init_test_buf();
	printgdf(buf_ch,"%e\n\n",-123e25);
	puts(test_buf);

	printf("%e\n",-123e-25);
	init_test_buf();
	printgdf(buf_ch,"%e\n\n",-123e-25);
	puts(test_buf);
#endif

printf(" %lx\n", (((unsigned long)-1)>>2)+ (((unsigned long)-1)>>1)  );


	{
		int width=0;
		char format[512];
		char sp_out[512];
//printf(" long = %d int = %d uint32 = %d\r\n",sizeof(long), sizeof(int), sizeof(uint32_t));
		for (width=0; width <15; width+=1 ) {
			long l=0;
			int i;

			sprintf(format," %%%.0dd %%%.0du  %%%.0dx %%%.0dX %%0%.0dx  %%0%.0dX  ||  %%%.0dld %%%.0dlu  %%%.0dlx %%%.0dlX %%0%.0dlx  %%0%.0dlX ||  %%%.0dlld %%%.0dllu  %%%.0dllx %%%.0dllX %%0%.0dllx  %%0%.0dllX \n",
					width,width,width,width,width,width, width,width,width,width,width,width, width,width,width,width,width,width);
			init_test_buf();
//			puts(format);
			for (i=0; i<10; i++, l=l*-3 +7) {
				unsigned int u;
				unsigned long ul= l;
				unsigned long long ull;

				u= ul & ((unsigned int) -1);
				ull =ul;
				sprintf(sp_out,format, u , u , u , u , u, u, ul, ul, ul, ul, ul, ul , ull, ull, ull, ull, ull, ull );


//				puts(sp_out);
				init_test_buf();
				printgdf(buf_ch, format, u , u , u , u , u, u, ul, ul, ul, ul, ul, ul , ull, ull, ull, ull, ull, ull );
				//puts(test_buf);

				if (strcmp(sp_out, test_buf) !=0 ) {
					printf("### differences !!\n");			
					puts(sp_out);
					puts(test_buf);
				}	
			}


		}
	}


	return 0;
}
#endif


#ifdef __cplusplus
}
#endif