
#define SOLAR_IN A6
#define SOLAR_OUT 7
#define HOUSE_OUT 9

// celsius
// supplementary power enabled (when appropriate) if over
#define TOO_HOT 30
// both power sources disabled if under
#define TOO_COLD 15

uint_fast8_t Solar_Status, House_Status;

void setup() {
	// set reference voltage - required for tempsense,
	// increases precision for SOLAR_IN readings.
	analogReference(INTERNAL1V1);
	pinMode(SOLAR_IN, INPUT);
	pinMode(SOLAR_OUT, OUTPUT);
	pinMode(HOUSE_OUT, OUTPUT);
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
	ADC0_CTRLD = (~ADC_INITDLY_gm & ADC0_CTRLD) | ADC_INITDLY_DLY64_gc;
	// set sampling delay to >= 32us (31 in samplen, 10 in sampdly)
	ADC0_SAMPCTRL = (~ADC_SAMPLEN_gm & ADC0_SAMPCTRL) | (31 << ADC_SAMPLEN_gp);
	ADC0_CTRLD = (~ADC_SAMPDLY_gm & ADC0_CTRLD) | (10 << ADC_SAMPDLY_gp);
	
	// in CTRLD set SAMPCAP to 1
	ADC0_CTRLD = (~ADC_SAMPCAP_bm & ADC0_CTRLD) | (1 << ADC_SAMPCAP_bp);
	// start a conversion to get the temperature
	ADC0_CTRLA |= 1 << ADC_ENABLE_bp;
	
	// this could really just be ADC0_COMMAND |= 1, but this is more portable, probably.
	ADC0_COMMAND = (~ADC_STCONV_bm & ADC0_COMMAND) | (1 << ADC_STCONV_bp);
	
	delay(1); // let everything sync up.
	
	uint32_t adc_reading = 0x03ff & ADC0_RES;
	
	// adjust for variance in devices (hardcoded into sigrow)
	adc_reading -= SIGROW_TEMPSENSE1;
	adc_reading *= SIGROW_TEMPSENSE0;
	
	// now holding 256 * temperature in kelvin.
	adc_reading -= (273.15 * (1 << 8));
	// now holding 256 * temperature in celsius.
	adc_reading += 0x80; // to ensure correct rounding.
	adc_reading >>= 8;
	// now in integer degrees celsius.
	return adc_reading;
}

void loop() {
	
}