#include <avr/sleep.h>

#define SOLAR_IN A6
#define SOLAR_OUT 7
#define HOUSE_OUT 9

// celsius
// supplementary power enabled (when appropriate) if over
#define TOO_HOT 30
// both power sources disabled if under
#define TOO_COLD 15

// minimum reading back from solar pin to indicate it supplies enough power.
#define SOLAR_THRESHOLD 12 /* todo */

uint_fast8_t Solar_Status, House_Status;

void setInterrupt() {
	// wait for startup to be done with PIT
	while (RTC_PITSTATUS & RTC_CTRLBUSY_bm) {}
	
	RTC_CLKSEL = RTC_CLKSEL_INT1K_gc;
	
	RTC_PITINTCTRL = (~RTC_PI_bm & RTC_PITINTCTRL) | (1 << RTC_PI_bp);
	
	RTC_PITCTRLA = (~RTC_PITEN_bm & RTC_PITCTRLA) | (1 << RTC_PITEN_bp) | RTC_PERIOD_CYC8192_gc;
}

ISR(RTC_PIT_vect) {
	// clear interrupt flag.
	RTC_PITINTFLAGS = RTC_PI_bm;
}

void setup() {
	// set reference voltage - required for tempsense,
	// increases precision for SOLAR_IN readings.
	analogReference(INTERNAL1V1);
	
	// revert ADC prescaler to board default (from arduino default)
	ADC0_CTRLC = (~ADC_PRESC_gm & ADC0_CTRLC) | ADC_PRESC_DIV16_gc;
	
	pinMode(SOLAR_IN, INPUT);
	pinMode(SOLAR_OUT, OUTPUT);
	pinMode(HOUSE_OUT, OUTPUT);
	digitalWrite(HOUSE_OUT, LOW);
	digitalWrite(SOLAR_OUT, LOW);
	Solar_Status = 1;
	House_Status = 0;
	delay(5);
	digitalWrite(SOLAR_OUT, HIGH);
	Serial.begin(9600);
	
	setInterrupt();
	set_sleep_mode(SLEEP_MODE_PWR_DOWN);
	sleep_enable();
}

int readTemp() {
	// quoting our way through the ATMega4809 datasheet
	// reference voltage set in setup() because we always use 1v1.
	
	// todo: figure out if CLK_MAIN on this model is 20MHz by default, in
	// which case I need to up the prescaler value.
	
	// select internal voltage reference by writing 0x0 to REFSEL in CTRLC
	ADC0_CTRLC &= (~ADC_REFSEL_gm);
	// select ADC temp sensor channel by configuring MUXPOS
	ADC0_MUXPOS = ADC_MUXPOS_TEMPSENSE_gc << ADC_MUXPOS_gp;
	// In ADC0_CTRLD select INITDLY >= 32 us x CLK_ADC
	ADC0_CTRLD = (~ADC_INITDLY_gm & ADC0_CTRLD) | ADC_INITDLY_DLY128_gc;
	// set sampling delay to >= 32us (31 in samplen, 10 in sampdly)
	ADC0_SAMPCTRL = (~ADC_SAMPLEN_gm & ADC0_SAMPCTRL) | (30 << ADC_SAMPLEN_gp);
	ADC0_CTRLD = (~ADC_SAMPDLY_gm & ADC0_CTRLD) | (0 << ADC_SAMPDLY_gp);
	
	// in CTRLD set SAMPCAP to 1
	ADC0_CTRLD = (~ADC_SAMPCAP_bm & ADC0_CTRLD) | (1 << ADC_SAMPCAP_bp);
	// start a conversion to get the temperature
	ADC0_CTRLA |= 1 << ADC_ENABLE_bp;
	
	ADC0_CTRLA &= ~ADC_FREERUN_bm;
	
	ADC0_CTRLB = (~ADC_SAMPNUM_gm & ADC0_CTRLB) | (ADC_SAMPNUM_ACC1_gc << ADC_SAMPNUM_gp);
	
	// this could really just be ADC0_COMMAND |= 1, but this is more portable, probably.
	ADC0_COMMAND = (~ADC_STCONV_bm & ADC0_COMMAND) | (1 << ADC_STCONV_bp);
	int t = (ADC0_COMMAND & ADC_STCONV_bm);
	
	Serial.println(t);
	
	delay(1); // let everything sync up.
	
	t = (ADC0_COMMAND & ADC_STCONV_bm);
	Serial.println(t);
	
	uint32_t adc_reading = 0x03ff & ((ADC0_RESH << 8) | ADC0_RESL);
	
	Serial.println(adc_reading);
	
	// adjust for variance in devices (hardcoded into sigrow)
	adc_reading -= SIGROW_TEMPSENSE1;
	adc_reading *= SIGROW_TEMPSENSE0;
	
	Serial.print("adc value: ");
	Serial.println(adc_reading);
	
	// now holding 256 * temperature in kelvin.
	adc_reading -= (273.15 * (1 << 8));
	// now holding 256 * temperature in celsius.
	adc_reading += 0x80; // to ensure correct rounding.
	adc_reading >>= 8;
	// now in integer degrees celsius.
	return adc_reading;
}

void loop() {
	Serial.println("Read temperature");
	int temp = readTemp();
	Serial.println(temp);
	Serial.println(temp > TOO_HOT);
	
	if (temp >= TOO_HOT) {
		// check if solar is providing enough voltage,
		// and enable house power if it's not.
		int solar_power = analogRead(SOLAR_IN);
		if (solar_power < SOLAR_THRESHOLD) {
			// we need house power. but, to make sure it's not just
			// a cloud or something, we'll make a couple attempts.
			
			if (Solar_Status & 0x80) {
				// swap to solar was in progress. mark it as too inconsistent.
				Solar_Status = 0;
			}
			
			if (House_Status & 0x80) {
				// we've already established on a previous loop the need to
				// switch to house power. if we've agreed 3 times, make the swap.
				if (House_Status & 0x03 == 3) {
					House_Status = 1;
					
					Serial.println("Disabling solar, enabling house");
					
					Solar_Status = 0;
					digitalWrite(SOLAR_OUT, LOW);
					delay(5); // give plenty of time for switch-off.
					digitalWrite(HOUSE_OUT, HIGH);
				} else {
					++House_Status;
				}
			} else if (House_Status == 1) {
				// house is already supplying power.
			} else {
				// house has not considered supplying power yet.
				House_Status = 0x81;
			}
		} else if (House_Status) {
			// solar is giving plenty.
			// ensure it's enabled and house is disabled, if solar has proven
			// consistent.
			
			if (House_Status & 0x80) {
				// were considering swapping to house power, but we're
				// still good over here.
				House_Status = 0;
			}
			
			if (Solar_Status & 0x80) {
				if (Solar_Status & 0x03 == 3) {
					
					Serial.println("Disabling house, enabling solar");
					
					Solar_Status = 1;
					House_Status = 0;
					digitalWrite(HOUSE_OUT, LOW);
					delay(5);
					digitalWrite(SOLAR_OUT, HIGH);
				} else {
					++Solar_Status;
				}
			} else if (Solar_Status == 1) {
				// solar is already power supply.
			} else {
				// solar hasn't considered supplying.
				Solar_Status = 0x81;
			}
		}
		
	} else if (temp <= TOO_COLD) {
		// turn off everything.
		if (Solar_Status || House_Status) {
			Serial.println("Disabling all power");
			digitalWrite(SOLAR_OUT, LOW);
			digitalWrite(HOUSE_OUT, LOW);
			Solar_Status = House_Status = 0;
		}
	} else {
		// house power off, let solar power do whatever it wants.
		if (House_Status == 1) {
			// house power was on. need to turn it off.
			Serial.println("Disabling house");
			digitalWrite(HOUSE_OUT, LOW);
		}
		if (House_Status) House_Status = 0;
		
		if (Solar_Status != 1) {
			Solar_Status = 1;
			delay(5); // make sure house power is cut.
			Serial.println("Enabling solar");
			digitalWrite(SOLAR_OUT, HIGH);
		}
	}
	
	// put to sleep, to be woken up by PIT
	Serial.println("Going to sleep");
	// sleep_cpu();
	delay(6000);
}