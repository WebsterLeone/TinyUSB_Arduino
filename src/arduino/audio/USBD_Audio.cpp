#include "USBD_Audio.h"
#include <tusb.h> // 
#include <class/audio/audio_device.h> // tud_audio_write, tud_audio_read
#include <class/audio/audio.h> // constants (AUDIO_FUNC_MUSICAL_INSTRUMENT)
#include <arduino/Adafruit_USBD_Device.h> // TinyUSBDevice.;
#include <cstdlib> // malloc/free

/**
  channels: number of channels (left, right...), currently supports 1.
  bitDepth: number of bits per sample, [8,10,12,16].
  bufferSampleDepth: number of samples to buffer, if 0 is provided external
    buffers must be passed to the update(...) function.
 */
USBD_Audio::USBD_Audio( uint8_t numberOfChannels, uint8_t bitDepth, uint16_t bufferSampleDepth )
  : channels( numberOfChannels ),
    bitDepth( bitDepth ),
    bufferSampleDepth( bufferSampleDepth ),
    bufferSize( channels * bitDepth * bufferSampleDepth ) {
  // Only allocate buffers if external buffers aren't being used
  if( bufferSampleDepth != 0 ) {
	txBuffer = (uint16_t*)malloc( bufferSize );
	rxBuffer = (uint16_t*)malloc( bufferSize );
  }
  //audioCollectionStrIndex = TinyUSBDevice.addStringDescriptor(AUDIO_COLLECTION_NAME);
  featureUnitStrIndex = TinyUSBDevice.addStringDescriptor(FEATURE_UNIT_NAME);
  outputTerminalStrIndex = TinyUSBDevice.addStringDescriptor(OUTPUT_TERMINAL_ANALOG_NAME);
  inputTerminalStrIndex = TinyUSBDevice.addStringDescriptor(INPUT_TERMINAL_ANALOG_NAME);
}

USBD_Audio::~USBD_Audio() {
  if( txBuffer != nullptr ) { free( txBuffer ); }
  if( rxBuffer != nullptr ) { free( rxBuffer ); }
}

bool USBD_Audio::begin() {
  // Don't run the setup multiple times
  if( alreadyAdded ) {
	  return false;
  }
  
  if (!TinyUSBDevice.addInterface(*this)) {
    return false;
  }

  alreadyAdded = true;
  return true;
}

bool USBD_Audio::update( uint8_t *txBuf, size_t *txCount, uint8_t *rxBuf, size_t rxCount ) {
  bool ok = false;
  // To USB host
  if( txBuf != nullptr && txCount > 0 ) {
    // tud_audio_write(...) parses buffer as byte (void) array
    uint32_t bytesWritten = tud_audio_write( txBuf, *txCount );

    if( bytesWritten == *txCount ) {
      // Entire buffer written, can clear it.
      *txCount = 0;
    } else if( bytesWritten == 0 ) {
      // Unable to write
      ok = false;
    } else {
      // Partial buffer written, compact it
      *txCount = *txCount - bytesWritten;
      memmove( txBuf, txBuf+bytesWritten, *txCount );
    }
  }

  // From USB host
  if( rxBuf != nullptr && rxCount > 0 ) {
    if( tud_audio_read( rxBuf, rxCount ) != rxCount ) {
      ok = false;
    }
  }
  return ok;
}

bool USBD_Audio::update( uint16_t *txBuf, size_t *txCount, uint16_t *rxBuf, size_t rxCount ) {
  *txCount *= 2;
  bool ok = update( (uint8_t*)txBuf, txCount, (uint8_t*)rxBuf, rxCount*2 );
  *txCount /= 2;
  return ok;
}

bool USBD_Audio::update() {
  return update( txBuffer, &txBufCount, rxBuffer, rxBufCount );
}

uint16_t USBD_Audio::getInterfaceDescriptor(uint8_t itfnum_deprecated,
  uint8_t *buf, uint16_t bufsize) {
  uint8_t itfCount = 2;
  uint8_t itfNum = TinyUSBDevice.allocInterface( itfCount );
  uint8_t ep_in  = TinyUSBDevice.allocEndpoint( TUSB_DIR_IN );
  uint8_t ep_out = TinyUSBDevice.allocEndpoint( TUSB_DIR_OUT );
  uint8_t pollingInterval = 100; // ??? guessing
  uint16_t maxPacketSize = 512; // ??? guessing
  
  // https://www.usb.org/sites/default/files/Audio2_with_Errata_and_ECN_through_Apr_2_2025.pdf
  uint16_t const desc_len =
    TUD_AUDIO_DESC_IAD_LEN + // Itf. Assoc. Descriptor: describes an Audio Interface Collection
    TUD_AUDIO_DESC_STD_AC_LEN + // Standard AudioControl Interface Descriptor
    TUD_AUDIO_DESC_CS_AC_LEN + // Class-specific AudioControl Interface Descriptor
// Lengths of the following block are added together as a value in the above CS AC Itf Desc.
    TUD_AUDIO_DESC_CLK_SRC_LEN + // External or Internal Fixed/Variable/Programmable Source
    TUD_AUDIO_DESC_INPUT_TERM_LEN + // USB out
    TUD_AUDIO_DESC_FEATURE_UNIT_ONE_CHANNEL_LEN + // Controls (Mute/Volume/Bass/Delay/etc)
    TUD_AUDIO_DESC_OUTPUT_TERM_LEN + // Line-out
	TUD_AUDIO_DESC_INPUT_TERM_LEN + // Analog in
    TUD_AUDIO_DESC_FEATURE_UNIT_ONE_CHANNEL_LEN + // Controls (Mute/Volume/Bass/Delay/etc)
	TUD_AUDIO_DESC_OUTPUT_TERM_LEN + //USB in
// End of block
    TUD_AUDIO_DESC_STD_AS_INT_LEN + // Std. AudioStream Itf. Descriptor (Alt 0, no EPs)
    TUD_AUDIO_DESC_STD_AS_INT_LEN + // Std. AudioStream Itf. Descriptor (Alt 1, streaming)
    TUD_AUDIO_DESC_CS_AS_INT_LEN + // Class-specific AudioStream Itf. Descriptor
    TUD_AUDIO_DESC_TYPE_I_FORMAT_LEN + // Type 1? Format Descriptor
    TUD_AUDIO_DESC_STD_AS_ISO_EP_LEN + // Std AudStrm Isochronous Audio Data EP Descriptor (4.10.1.1)
    TUD_AUDIO_DESC_CS_AS_ISO_EP_LEN + // Class-specific AS Isochronous Audio Data EP Descriptor (4.10.1.2)
    TUD_AUDIO_DESC_STD_AS_ISO_FB_EP_LEN; //Std AudStrm Isochronous Feedback EP Descriptor(4.10.2.1)
  
  // null buffer is used to get the length of descriptor only
  if (!buf) {
    return desc_len;
  }

  // supplied buffer is too small
  if (bufsize < desc_len) {
    return 0;
  }
  
  uint16_t len = 0;

  // Header of the Audio Interface Collection
  {
    uint8_t desc[] = {TUD_AUDIO_DESC_IAD(itfNum, itfCount, _strid )};
    memcpy(buf + len, desc, sizeof(desc));
    len += sizeof(desc);
  }
  
  // Standard AudioControl Interface Descriptor
  {
    uint8_t numEP = 0; // Not using interrupts, no need for control EP
    uint8_t strIdx = 0; // None defined
    uint8_t desc[] = {TUD_AUDIO_DESC_STD_AC(itfNum, numEP, strIdx )};
    memcpy(buf + len, desc, sizeof(desc));
    len += sizeof(desc);
  }

  // Class-Specific AudioControl Interface (describes audio functions)
  {
    uint16_t bcdADC = 2; // Audio Device Class Specification Release in BCD
    uint8_t category = AUDIO_FUNC_MUSICAL_INSTRUMENT; // 0x09 == Musical Instrument
	// totalLen is length of all clock source, feature unit, and terminal descriptors combined
    uint16_t totalLen = TUD_AUDIO_DESC_CLK_SRC_LEN + TUD_AUDIO_DESC_INPUT_TERM_LEN * 2 + TUD_AUDIO_DESC_OUTPUT_TERM_LEN *2 + TUD_AUDIO_DESC_FEATURE_UNIT_ONE_CHANNEL_LEN;
    uint8_t ctrl = 0x00; // D7..2 = RSVD, set to 0; 1..0 Latency Control Available (no = b00)
    uint8_t desc[] = {TUD_AUDIO_DESC_CS_AC( bcdADC, category, totalLen, ctrl )};
    memcpy(buf + len, desc, sizeof(desc));
    len += sizeof(desc);
  }

  uint8_t id = 1;
  uint8_t groupClockID = id++;
  // Clock Source
  {
    // Unique clock ID in this audio function
	uint8_t clockID = groupClockID;
	// D7..3: b00000 Reserved
	// D2   : b1 = Synced to SOF, 0 = Not Synced
    // D1..0: b00 = External Clock, 01: Internal Fixed, 10: Int. Variable, 11: Int. Programmable
    uint8_t attr = 0x01;
	// D7..4: b0000 Reserved
    // D3..2: Validity Control : b00 = not present, b01 = read-only, b11 = writable
    // D1..0: Frequency Control: b00 = not present, b01 = read-only, b11 = writable
    uint8_t ctrl = 0x00;
	// ID of input/output terminal associated with this clock source
    uint8_t assocTermID = 0; //clockID + itfCount; // Useful when clock is derived from e.g. an input's clock-recovery circuit
    uint8_t strIdx = 0; // None. Add in constructor if desired and change this.
    uint8_t desc[] = {TUD_AUDIO_DESC_CLK_SRC( clockID, attr, ctrl, assocTermID, strIdx )};
    memcpy(buf + len, desc, sizeof(desc));
    len += sizeof(desc);
  }

  uint8_t streamingTermID = id++;
  // Input Terminal: out from USB host into device
  {
    uint8_t termID = streamingTermID;
    uint16_t termType = 0x101; // USB streaming
    uint8_t assocTerm = termID + 2; // Output it's associated with
    uint8_t clkID = groupClockID; // Clock used by this terminal
    uint8_t channels = 1;
    uint32_t channelSpatialCfg = 0x0; // No spatial data
    uint8_t ch1nameID = 0; // Add in constructor.
    // Controls Not Available (b00), Available (b01), or Writable (b11)
    // D15..14 Rsvd, D13..12 Phantom Power, D11..10 Overflow, D9..8 Underflow, D7..6 Cluster
    // D5..4 Overload, D3..2 Connector, D1..0 Copy Protection
    uint16_t controls = 0x0000;
    uint8_t strIdx = 0;
    uint8_t desc[] = {TUD_AUDIO_DESC_INPUT_TERM( termID, termType, assocTerm, clkID, channels, channelSpatialCfg, ch1nameID, controls, strIdx )};
    memcpy(buf + len, desc, sizeof(desc));
    len += sizeof(desc);
  }

  // Feature Unit (Functions)
  {
    uint8_t unitID = id++;
	uint8_t srcID = id-1; // Input it's associated with
    // Controls Not Available (b00), Available (b01), or Writable (b11)
    // D31..30 HPF, D29..28 Overflow, D27..26 Underflow, D25..24 Phase Inverter
    // D23..22 Input Gain Atten, D21..20 Input Gain, D19..18 Loudness,
    // D17..16 Bass boost, D15..14 Delay, D13..12 AGC, D11..10 Graphical EQ,
    // D9..8 Treble, D7..6 Mid, D5..4 Bass, D3..2 Volume, D1..0 Mute
    uint32_t masterControls = 0x00000003; // master control is "channel 0"
	uint32_t ch1Controls = 0x00000000; // No individual logical channel control
    uint8_t strIdx = featureUnitStrIndex;
    uint8_t desc[] = {TUD_AUDIO_DESC_FEATURE_UNIT_ONE_CHANNEL( unitID, srcID, masterControls, ch1Controls, strIdx )};
    memcpy(buf + len, desc, sizeof(desc));
    len += sizeof(desc);
  }

  // Output Terminal: analog output to DAC or I2S interface
  {
    uint8_t termID = id++;
    uint16_t termType = 0x601;      // Generic analog connector
    uint8_t assocTerm = termID - 2; // Input it's associated with
	uint8_t srcID = termID - 1;     // Feature Unit this output is associated with
    uint8_t clkID = groupClockID;   // Clock used by this terminal
    // Controls Not Available (b00), Available (b01), or Writable (b11)
    // D15..10 Rsvd, D9..8 Overflow, D7..6 Underflow,
    // D5..4 Overload, D3..2 Connector, D1..0 Copy Protection
    uint16_t controls = 0x0000;
    uint8_t strIdx = outputTerminalStrIndex;
    uint8_t desc[] = {TUD_AUDIO_DESC_OUTPUT_TERM( termID, termType, assocTerm, srcID, clkID, controls, strIdx )};
    memcpy(buf + len, desc, sizeof(desc));
    len += sizeof(desc);
  }
  
  // Input Terminal: analog input from ADC or I2S interface
  {
    uint8_t termID = id++;
    uint16_t termType = 0x601; // Generic analog connector
    uint8_t assocTerm = termID + 2; // Output it's associated with
    uint8_t clkID = groupClockID; // Clock used by this terminal
    uint8_t channels = 1;
    uint32_t channelSpatialCfg = 0x00; // No spatial data
    uint8_t ch1nameID = 0; // Add in constructor.
    // Controls Not Available (b00), Available (b01), or Writable (b11)
    // D15..14 Rsvd, D13..12 Phantom Power, D11..10 Overflow, D9..8 Underflow, D7..6 Cluster
    // D5..4 Overload, D3..2 Connector, D1..0 Copy Protection
    uint16_t controls = 0x0000;
    uint8_t strIdx = inputTerminalStrIndex;
    uint8_t desc[] = {TUD_AUDIO_DESC_INPUT_TERM( termID, termType, assocTerm, clkID, channels, channelSpatialCfg, ch1nameID, controls, strIdx )};
    memcpy(buf + len, desc, sizeof(desc));
    len += sizeof(desc);
  }

  // Feature Unit (Functions)
  {
    uint8_t unitID = id++;
	uint8_t srcID = id-1; // Input it's associated with
    // Controls Not Available (b00), Available (b01), or Writable (b11)
    // D31..30 HPF, D29..28 Overflow, D27..26 Underflow, D25..24 Phase Inverter
    // D23..22 Input Gain Atten, D21..20 Input Gain, D19..18 Loudness,
    // D17..16 Bass boost, D15..14 Delay, D13..12 AGC, D11..10 Graphical EQ,
    // D9..8 Treble, D7..6 Mid, D5..4 Bass, D3..2 Volume, D1..0 Mute
    uint32_t masterControls = 0x00000003; // master control is "channel 0"
	uint32_t ch1Controls = 0x00000000; // No individual logical channel control
    uint8_t strIdx = featureUnitStrIndex;
    uint8_t desc[] = {TUD_AUDIO_DESC_FEATURE_UNIT_ONE_CHANNEL( unitID, srcID, masterControls, ch1Controls, strIdx )};
    memcpy(buf + len, desc, sizeof(desc));
    len += sizeof(desc);
  }

  // Output Terminal: out from device into USB host
  {
    uint8_t termID = id++;
    uint16_t termType = 0x101;      // USB streaming
    uint8_t assocTerm = termID - 2; // Input it's associated with
	uint8_t srcID = termID - 1;     // Feature Unit this output is associated with
    uint8_t clkID = groupClockID;   // Clock used by this terminal
    // Controls Not Available (b00), Available (b01), or Writable (b11)
    // D15..10 Rsvd, D9..8 Overflow, D7..6 Underflow,
    // D5..4 Overload, D3..2 Connector, D1..0 Copy Protection
    uint16_t controls = 0x0000;
    uint8_t strIdx = 0; // None
    uint8_t desc[] = {TUD_AUDIO_DESC_OUTPUT_TERM( termID, termType, assocTerm, srcID, clkID, controls, strIdx )};
    memcpy(buf + len, desc, sizeof(desc));
    len += sizeof(desc);
  }
  
  // Std. AudioStreaming Itf. Descriptor (Alt 0, no EPs) (4.9.1)
  {
	uint8_t altSet = 0x00; // Itf. used when insufficient bandwidth for isochronous streaming
	// No data EP (0x00), Data EP (0x01), or Data + Explicit Feedback EP (0x02)
    uint8_t numEPs = 0;     // Using general data endpoints? (not sure how this fallback works)
    uint8_t strIdx = 0;    // None
    uint8_t desc[] = {TUD_AUDIO_DESC_STD_AS_INT( itfNum, altSet, numEPs, strIdx )};
    memcpy(buf + len, desc, sizeof(desc));
    len += sizeof(desc);	
  }
  // Std. AudioStreaming Itf. Descriptor (Alt 1, streaming)
  {
	uint8_t altSet = 0x01;    // Itf. used for isochronous streaming
	// No data EP (0x00), Data EP (0x01), or Data + Explicit Feedback EP (0x02)
    uint8_t numEPs = itfCount; // Using endpoints for full-duplex isochronous streaming
    uint8_t strIdx = 0;       // None
    uint8_t desc[] = {TUD_AUDIO_DESC_STD_AS_INT( itfNum, altSet, numEPs, strIdx )};
    memcpy(buf + len, desc, sizeof(desc));
    len += sizeof(desc);	
  }
  // Class-specific AudioStreaming Itf. Descriptor (4.9.2)
  {
    uint8_t termID = streamingTermID; // ID of terminal this is associated with
    // Controls Not Available (b00), Available (b01), or Writable (b11)
	// D7..4: Rsvd; D3..2: Valid Alt Settings Ctrl; D1..0 Active Alt Settings Ctrl
	uint8_t ctrl = 0x00;
	uint8_t formatType = 0x01; // Format Type I (see below)
	uint32_t formats = 0x00; // PCM only, see "USB D.C. for Audio Data Formats" A.2.1
	uint8_t numPhysChs = 0x01; // Number of physical channels in AS Itf channel cluster
	uint32_t chCfg = 0x00000004; // Spatial location of physical channels (4.1) -- front-center
    uint8_t strIdx = 0; // None // Name of first physical name
    uint8_t desc[] = {TUD_AUDIO_DESC_CS_AS_INT( termID, ctrl, formatType, formats, numPhysChs, chCfg, strIdx )};
    memcpy(buf + len, desc, sizeof(desc));
    len += sizeof(desc);	
  }
  // Type 1 Format Descriptor ("USB Device Classification for Audio Data Formats" 2.3.1.6)
  { 
    uint8_t subslotSize = 2; // Bytes per sample, rounded up
	uint8_t bitDepth = 16; // Bits per sample (less than or equal to subslotSize*2 bits)
    uint8_t desc[] = {TUD_AUDIO_DESC_TYPE_I_FORMAT( subslotSize, bitDepth )};
    memcpy(buf + len, desc, sizeof(desc));
    len += sizeof(desc);	
  }
//*********** DO WE NEED TWO FOR IN/OUT???? ***************************************************//
  // Std AudStrm Isochronous Audio Data EP Descriptor (4.10.1.1)
  {
	// D7: b0 = out/b1 = in; D6..4: rsvd; D3..0: endpoint number
	uint8_t endpoint = ep_out;
	// D7..6: rsvd; D5..4: b00 = data, b10 = implicit FB data EP;
	// D3..2: b01 = async, b10 = adaptive, b11 = sync, D1..0: 01 = isochronous
	uint8_t attr = 0x05; // asynchronous data-only
	uint16_t maxPktSize = maxPacketSize; // maximum packet size this endpoint can send/receive
	uint8_t interval = pollingInterval; // polling interval for data transfers
    uint8_t desc[] = {TUD_AUDIO_DESC_STD_AS_ISO_EP( endpoint, attr, maxPktSize, interval )};
    memcpy(buf + len, desc, sizeof(desc));
    len += sizeof(desc);	
  }
  // Class-specific AS Isochronous Audio Data EP Descriptor (4.10.1.2)
  {
	// D7: b1 = packets must be max size specified above, b0 = can be short
	uint8_t attr = 0x00; // uC can handle short packets (I hope)
    // Controls Not Available (b00), Available (b01), or Writable (b11)
	// D7..6: rsvd; D5..4: Data underrun control; D3..2: Data overrun control;
	// D1..0: pitch (variable sample rate) control
	uint8_t ctrl = 0x00;
	uint8_t lockDelayUnit = 0x00; // We are running on an internal clock so there is no lock delay
	uint16_t lockDelay = 0x0000; // Ditto
    uint8_t desc[] = {TUD_AUDIO_DESC_CS_AS_ISO_EP( attr, ctrl, lockDelayUnit, lockDelay )};
    memcpy(buf + len, desc, sizeof(desc));
    len += sizeof(desc);	
  }
  //Std AudStrm Isochronous Feedback EP Descriptor(4.10.2.1)
  {
	// D7: b0 = out/b1 = in; D6..4: rsvd; D3..0: endpoint number
	uint8_t endpoint = ep_in;
	uint16_t epSize = maxPacketSize;
	uint8_t interval = pollingInterval; // polling interval for data transfers
    uint8_t desc[] = {TUD_AUDIO_DESC_STD_AS_ISO_FB_EP( endpoint, epSize, interval )};
    memcpy(buf + len, desc, sizeof(desc));
    len += sizeof(desc);	
  }
  
  if (len != desc_len) {
    // TODO should throw an error message
    return 0;
  }

  return desc_len;
}