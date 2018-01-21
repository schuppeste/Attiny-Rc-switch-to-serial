#include <RCSwitch.h>
RCSwitch mySwitch = RCSwitch();
// Set your baud rate and number of stop bits here
#define BAUDRATE            9600
#define STOPBITS            1
//F_CPU defined by Arduino, e.g. 1000000, 8000000, 16000000
#define F_CPU 8000000

// If bit width in cpu cycles is greater than 255 then  divide by 8 to fit in timer
// Calculate prescaler setting
#define CYCLES_PER_BIT       ( (8000000) / (BAUDRATE) )
#if (CYCLES_PER_BIT > 255)
#define DIVISOR             8
#define CLOCKSELECT         2
#else
#define DIVISOR             1
#define CLOCKSELECT         1
#endif
#define FULL_BIT_TICKS      ( (CYCLES_PER_BIT) / (DIVISOR) )

// USISerial send state variable and accessors
enum USISERIAL_SEND_STATE { AVAILABLE, FIRST, SECOND };
static volatile enum USISERIAL_SEND_STATE usiserial_send_state = AVAILABLE;
static inline enum USISERIAL_SEND_STATE usiserial_send_get_state(void)
{
  return usiserial_send_state;
}
static inline void usiserial_send_set_state(enum USISERIAL_SEND_STATE state)
{
  usiserial_send_state = state;
}
bool usiserial_send_available()
{
  return usiserial_send_get_state() == AVAILABLE;
}

// Transmit data persistent between USI OVF interrupts
static volatile uint8_t usiserial_tx_data;
static inline uint8_t usiserial_get_tx_data(void)
{
  return usiserial_tx_data;
}
static inline void usiserial_set_tx_data(uint8_t tx_data)
{
  usiserial_tx_data = tx_data;
}

static uint8_t reverse_byte (uint8_t x) {
  x = ((x >> 1) & 0x55) | ((x << 1) & 0xaa);
  x = ((x >> 2) & 0x33) | ((x << 2) & 0xcc);
  x = ((x >> 4) & 0x0f) | ((x << 4) & 0xf0);
  return x;
}
void usiserial_send_byte(uint8_t data)
{
  while (usiserial_send_get_state() != AVAILABLE)
  {
    // Spin until we finish sending previous packet
  };
  usiserial_send_set_state(FIRST);
  usiserial_set_tx_data(reverse_byte(data));
  TCCR1 = 0;
  TCCR1 |= (1 << CTC1);                    // CTC mode
  TCCR1 |= (0 << CS10);                // Set prescaler to clk or clk /8
  TCCR1 |= (0 << CS11);
  TCCR1 |= (1 << CS12);
  TCCR1 |= (0 << CS13);
  GTCCR |= (1 << PSR1);
  TIMSK |= (1 << OCIE1A);
  OCR1C = FULL_BIT_TICKS;                // Trigger every full bit width
  TCNT1 = 0;                    // Count up from 0

  // Configure USI to send high start bit and 7 bits of data
  USIDR = 0x00 |                            // Start bit (low)
          usiserial_get_tx_data() >> 1;         // followed by first 7 bits of serial data
  USICR  = (1 << USIOIE) |                  // Enable USI Counter OVF interrupt.
           (0 << USIWM1) | (1 << USIWM0) |       // Select three wire mode to ensure USI written to PB1
           (0 << USICS1) | (0 << USICS0) | (0 << USICLK); // Select Timer0 Compare match as USI Clock source.
  DDRB  |= (1 << PB1);                      // Configure USI_DO as output.
  USISR = 1 << USIOIF |                     // Clear USI overflow interrupt flag
          (16 - 8);                             // and set USI counter to count 8 bits
}

ISR (TIM1_COMPA_vect)
{
 //Shift Usi Bitclock to next Clock
  USICR  |= (1 << USICLK);

}// USI overflow interrupt indicates we have sent a buffer
ISR (USI_OVF_vect) {
  if (usiserial_send_get_state() == FIRST)
  {
    usiserial_send_set_state(SECOND);
    USIDR = usiserial_get_tx_data() << 7  // Send last 1 bit of data
            | 0x7F;                           // and stop bits (high)
    USISR = 1 << USIOIF |                 // Clear USI overflow interrupt flag
            (16 - (1 + (STOPBITS)));          // Set USI counter to send last data bit and stop bits
  }
  else
  {
    PORTB |= 1 << PB1;                    // Ensure output is high
    DDRB  |= (1 << PB1);                  // Configure USI_DO as output.
    USICR = 0;                            // Disable USI.
    USISR |= 1 << USIOIF;                 // clear interrupt flag
    TCCR1 = 0;
    usiserial_send_set_state(AVAILABLE);
  }
}

void setup() {
  TCCR1 = 0;

  mySwitch.enableReceive(0);
  pinMode(1, HIGH);               // Configure USI_DO as output.
  digitalWrite(1, HIGH);          // Ensure serial output is high when idle
}

String test = "";
long temp = 0;
void loop() {

  if (mySwitch.available()) {
    
    test = "";
    test +=  String(mySwitch.getReceivedValue());
    test += "\n";
    char text [test.length()];
    test.toCharArray(text, test.length()) ;
    uint8_t len = sizeof(text) - 1;
    for (uint8_t i = 0; i < 8; i++)
    {
      while (!usiserial_send_available())
      {
        // Wait for last send to complete
      }
      usiserial_send_byte(text[i]);
    }
    mySwitch.resetAvailable();
  }
}
