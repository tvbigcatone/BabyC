
/*-----------------------------------------------+--------------------------------------------------
                                             INCLUDE FILES
--------------------------------------------------------------------------------------------------*/
#include <stdarg.h>
#include <stdbool.h>
unsigned int mini_strlen(const char *s);

#if 1
/*-----------------------------------------------+--------------------------------------------------
                                             PRIVATE DEFINES
--------------------------------------------------------------------------------------------------*/
struct mini_buff {
	char *buffer, *pbuffer;
	unsigned int buffer_len;
};

/*-----------------------------------------------+--------------------------------------------------
                                      PRIVATE FUNCTION PROTOTYPES
--------------------------------------------------------------------------------------------------*/

static unsigned int mini_itoa(int value, 
                              unsigned int radix, 
							  unsigned int uppercase, 
							  unsigned int unsig, 
							  char *buffer, 
							  unsigned int zero_pad );

static unsigned int mini_ftoa(double value, 
                              char *buffer, 
                              unsigned int afterpoint);                              
                              
static int _putc(int ch, struct mini_buff *b);
static int _puts(char *s, unsigned int len, struct mini_buff *b);

/*===============================================+==================================================
                                       PUBLIC FUNCTIONS
==================================================================================================*/

int mini_vsnprintf(char *buffer, unsigned int buffer_len, const char *fmt, va_list va)
{
	struct mini_buff b;
	char bf[24];
	char ch;

    char zero_pad = 0, afterpoint = 7; // default precision = 7 digits. Single-precision floating-point numbers have a 24-bit mantissa, which is approximately 7.2 decimal digits
    char *ptr, *pad;
	unsigned int len;
    unsigned int result_len = 0;
    
	b.buffer = buffer;
	b.pbuffer = buffer;
	b.buffer_len = buffer_len;

    ch=*(fmt);

    while( ch )
    {
        fmt++;
                
		if( ch!='%' )
        {
			_putc(ch, &b);
            result_len++;
		}
        else 
        {
			zero_pad = 0;
			ch=*(fmt++);
            
            if (ch=='0') pad = &zero_pad;
            if (ch=='.') pad = &afterpoint;
				
			/* Zero padding requested */
			if (ch=='0' || ch=='.') 
            {
				if (ch=='.') { do{ ch=*(fmt++); } while(ch=='0'); }
                else         { ch=*(fmt++);                       }
				if (ch == '\0')
					goto end;
				if (ch >= '0' && ch <= '9') *pad = ch - '0';
				ch=*(fmt++);
			}
            
			switch( ch ) 
            {
				case 0:
					goto end;

				case 'u':
				case 'd':
					len = mini_itoa(va_arg(va, unsigned int), 10, 0, (ch=='u'), bf, zero_pad);
					_puts(bf, len, &b);
                    result_len += len;
					break;

				case 'x':
				case 'X':
					len = mini_itoa(va_arg(va, unsigned int), 16, (ch=='X'), 1, bf, zero_pad);
					_puts(bf, len, &b);
                    result_len += len;
					break;

				case 'c' :
					_putc((char)(va_arg(va, int)), &b);
                    result_len++;
					break;

				case 's' :
					ptr = va_arg(va, char*);
                    len = mini_strlen(ptr);
					_puts(ptr, len, &b);
                    result_len += len;
					break;
                
				case 'f':
					len = mini_ftoa(va_arg(va, double), bf, afterpoint);
					_puts(bf, len, &b);
                    result_len += len;
					break;

				default:
					_putc(ch, &b);
                    result_len++;
					break;
			} // switch ch
		}
        ch=*(fmt);
	} // while( ch )

    /* vsnprintf ALWAYS put last char in buffer to '\0' */
    if(buffer_len && (result_len < buffer_len) )
    {
        buffer[result_len] = '\0';
    }
    else if(buffer_len)
    {
        buffer[buffer_len-1] = '\0';
    }

end:
    /* vsnprintf return number of char EXCLUDED final '\0' */
    return result_len;
}

int mini_snprintf(char* buffer, unsigned int buffer_len, const char *fmt, ...)
{
	int ret;
	va_list va;
	va_start(va, fmt);
	ret = mini_vsnprintf(buffer, buffer_len, fmt, va);
	va_end(va);

	return ret;
}

unsigned int mini_strlen(const char *s)
{
	unsigned int len = 0;
	while (s[len] != '\0') len++;
	return len;
}

/*===============================================+==================================================
                                       PRIVATE FUNCTIONS
==================================================================================================*/

static unsigned int mini_itoa(int value, unsigned int radix, unsigned int uppercase, unsigned int unsig,
	 char *buffer, unsigned int zero_pad)
{
	char *p = buffer;
	int	negative = 0;
	unsigned int	i, len;

	/* No support for unusual radixes. */
	if (radix > 16)
		return 0;

	if (value < 0 && !unsig) {
		negative = 1;
		value = -value;
	}

	/* This builds the string back to front ... */
	do {
		int digit = value % radix;
		*(p++) = (digit < 10 ? '0' + digit : (uppercase ? 'A' : 'a') + digit - 10);
		value /= radix;
	} while (value > 0);

	for (i = (p - buffer); i < zero_pad; i++)
		*(p++) = '0';

	if (negative)
		*(p++) = '-';

	*(p) = '\0';

	/* ... now we reverse it (could do it recursively but will
	 * conserve the stack space) */
	len = (p - buffer);
	for (i = 0; i < len / 2; i++) {
		char j = buffer[i];
		buffer[i] = buffer[len-i-1];
		buffer[len-i-1] = j;
	}

	return len;
}

// Converts a floating point number to string. 
static unsigned int mini_ftoa(double value, char *buffer, unsigned int afterpoint) 
{ 
    char *p = buffer;
    unsigned int len = 0;

    if (value < 0) {
		value = -value;
        *p = '-'; len++;
	}
    
    // Extract integer part 
    int ipart = (int)value; 
    // Extract floating part 
    float fpart = value - (float)ipart; 
    
    // convert integer part to string 
    len += mini_itoa(ipart, 10, false, false, p + len, 0);  
 
    // check for display option after point 
    if(afterpoint != 0) 
    { 
        p[len++] = '.';  // add dot
        // Get the value of fraction part upto given no. 
        // of points after dot. The third parameter is needed 
        // to handle cases like 233.007 
        uint32_t pow10 = 0x00000001;
        for (int n = 0; n<afterpoint; n++ ) pow10 = ((pow10<<1) + (pow10<<3)); 
        fpart *= (float)pow10;
        len += mini_itoa((int)fpart, 10, false, false, p + len, afterpoint); 
    }
    
    return len;
} 

static int _putc(int ch, struct mini_buff *b)
{
	if ((unsigned int)((b->pbuffer - b->buffer) + 1) >= b->buffer_len)
		return 0;

    if( (b->pbuffer - b->buffer) < b->buffer_len )
    {
        *(b->pbuffer++) = ch;
    }

	return 1;
}

static int _puts(char *s, unsigned int len, struct mini_buff *b)
{
	unsigned int i;

	if (b->buffer_len - (b->pbuffer - b->buffer) - 1 < len)
		len = b->buffer_len - (b->pbuffer - b->buffer) - 1;

	/* Copy to buffer */
	for (i = 0; i < len; i++)
    {
        if( (b->pbuffer - b->buffer) < b->buffer_len )
        {
            *(b->pbuffer++) = s[i];
        }
    }

	return len;
}

#endif
