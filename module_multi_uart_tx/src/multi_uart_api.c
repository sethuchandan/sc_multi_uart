#include "multi_uart_tx.h"

s_multi_uart_tx_channel uart_tx_channel[UART_TX_CHAN_COUNT];

unsigned crc8_helper(unsigned *checksum, unsigned data, unsigned poly);

/**
 * Calculate if the baud rate is valid for the defined divider 
 * @param baud  Requested baud rate                                                 
 * @return      Divider on success (i.e. >=1), 0 on error
 */
static int uart_tx_calc_baud( int baud )
{
    int max_baud = UART_CLOCK_RATE_HZ / UART_CLOCK_DIVIDER;
    
    /* check we are not requesting a value greater than the max */
    if (baud > max_baud)
        return 0;
    
    /* check we divide exactly */
    if (max_baud % baud != 0)
        return 0;
    
    return (max_baud / baud); // return clock divider
}

/**
 * Calculate parity according to the configuration
 * @param channel_id    Channel identifier
 * @param uart_char     The character being sent
 * @return              Parity value
 */
static unsigned uart_tx_calc_parity(int channel_id, unsigned int uart_char)
{
    unsigned p;
    
    switch (uart_tx_channel[channel_id].parity_mode)
    {
        case mark:
            return 1;
        case space:
            return 0;
        default:
            break;
    }
        
    if (uart_tx_channel[channel_id].uart_char_len != 8)
    {
        /* manually calculate parity */
        p = uart_char & 1;
        for (int i = 1; i < uart_tx_channel[channel_id].uart_char_len; i++)
        {
            p ^= ((uart_char >> i) & 1);
        }
        
    }
    else
    {
        int poly = 0x1;
        crc8_helper(&p, uart_char, poly);
    }
    
    switch (uart_tx_channel[channel_id].parity_mode)
    {
        case even:
            return p;
        case odd :
            return (!p);
        default:
            return (-1); // should never reach here
            break;
    }
}

/**
 * Configure the UART channel
 * @param channel_id    Channel Identifier
 * @param op_mode       Mode of operation
 * @param baud          Required baud rate
 * @param char_len      Length of a character in bits (e.g. 8 bits)
 * @return              Return 0 on success
 */
int uart_tx_initialise_channel( int channel_id, e_uart_tx_config_parity parity, e_uart_tx_config_stop_bits stop_bits, int baud, int char_len )
{
    /* check and calculate baud rate divider */
    if ((uart_tx_channel[channel_id].clocks_per_bit = uart_tx_calc_baud(baud)) == 0)
        return 1;
    
    /* set operation mode */
    uart_tx_channel[channel_id].sb_mode = stop_bits;
    uart_tx_channel[channel_id].parity_mode = parity;
    
    /* set the uart character length */
    uart_tx_channel[channel_id].uart_char_len = char_len;
    
    /* calculate word length */
    uart_tx_channel[channel_id].uart_word_len += 1; // start bit
    switch (parity)
    {
        case odd:
        case even:
        case mark:
        case space:
            uart_tx_channel[channel_id].uart_word_len += 1;
            break;
        case none:
            break;
    }
    
    switch (stop_bits)
    {
        case sb_1:
            uart_tx_channel[channel_id].uart_word_len += 1;
            break;
        case sb_2:
            uart_tx_channel[channel_id].uart_word_len += 2;
            break;
    }
    
    uart_tx_channel[channel_id].uart_word_len += char_len;
    
    /* calculate buffer increment value */
    uart_tx_channel[channel_id].inc = uart_tx_channel[channel_id].uart_word_len / 8;
    if (uart_tx_channel[channel_id].inc * 4 < uart_tx_channel[channel_id].uart_word_len)
        uart_tx_channel[channel_id].inc += 1;
    
    /* set max number of elements in buffer */
    uart_tx_channel[channel_id].nMax = UART_TX_BUF_SIZE / uart_tx_channel[channel_id].inc;
    
    /* zero the buffer pointers */
    uart_tx_channel[channel_id].wr_ptr = 0;
    uart_tx_channel[channel_id].rd_ptr = 0;
    
    /* clean buffer */
    for (int i = 0; i < UART_TX_BUF_SIZE; i++)
        uart_tx_channel[channel_id].buf[i] = 0;
    
    /* clean other variables */
    uart_tx_channel[channel_id].current_word = 0;
    uart_tx_channel[channel_id].current_word_pos = 0;
    
    return 0;
}

/**
 * Calculate tx full word
 * @param channel_id    Channel identifier
 * @param uart_char     The character being sent
 * @return              Full UART word in the format (msb -> lsb) STOP|PARITY|DATA|START 
 */
unsigned int uart_tx_calc_word( int channel_id, unsigned int uart_char )
{
    unsigned int full_word;
    int temp;
    int pos;
    
    /* format data into the word (msb -> lsb) STOP|PARITY|DATA|START */
    
    /* start bit */
    full_word = 1;
    pos += 1;
    
    /* uart word - mask, reverse char and put into full word */
    temp = (((1 << uart_tx_channel[channel_id].uart_char_len) - 1) & uart_char);
    temp = bitrev(temp) >> (32-uart_tx_channel[channel_id].uart_char_len);
    full_word |=  temp << pos;
    pos += uart_tx_channel[channel_id].uart_char_len;
    
    /* parity */
    if (uart_tx_channel[channel_id].parity_mode != none)
    {
        full_word |= (uart_tx_calc_parity(channel_id, uart_char) << pos);
        pos += 1;
    }
    
    /* stop bit */
    switch (uart_tx_channel[channel_id].sb_mode)
    {
        case sb_1:
            full_word |= 1 << pos;
            break;
        case sb_2:
            full_word |= 0x3 << pos;
            break;
    }
    
    return full_word;
}

