#ifndef USBD_AUDIO_H
#define USBD_AUDIO_H

#include <Arduino.h> // size_t, etc
#include <ports/samd/tusb_config_samd.h>

class USBD_Audio : public Adafruit_USBD_Interface {
  // Descriptor strings
  static constexpr const char *AUDIO_COLLECTION_NAME = "AURA MGP01";
  static constexpr const char *FEATURE_UNIT_NAME = "AURA Controls";
  static constexpr const char *INPUT_TERMINAL_ANALOG_NAME = "Analog in";
  static constexpr const char *OUTPUT_TERMINAL_ANALOG_NAME = "Analog out";
  // Number of samples per channel to buffer
  static const size_t DEFAULT_BUFFER_SAMPLES = 32;
  
  // Descriptor string IDs
  uint8_t *sourceStrIDs; // Main name for setStringDescriptor
  uint8_t audioCollectionStrIndex = 0;
  uint8_t featureUnitStrIndex = 0;
  uint8_t inputTerminalStrIndex = 0;
  uint8_t outputTerminalStrIndex = 0;
  // Digital audio attributes
  const uint8_t channels;
  const uint8_t bitDepth;
  // Digital audio buffer
  const uint16_t bufferSampleDepth;
  const uint16_t bufferSize;
  uint16_t *rxBuffer, *txBuffer;
  size_t rxBufCount = 0, txBufCount = 0;
  // State of audio interface
  bool alreadyAdded = false;
  
  public:
  USBD_Audio( uint8_t numberOfChannels = CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX,
    uint8_t bitDepth = CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX*8,
    uint16_t bufferSampleDepth = DEFAULT_BUFFER_SAMPLES );
  ~USBD_Audio();
  
  // Initialization (adds to TinyUSBDevice device list)
  bool begin();
  /* An update(...) call must be run repeatedly to copy data to/from buffers.
   * If a non-zero bufferSampleDepth was supplied to the constructor, this
   * object creates internal buffers which can be used with update(). If it
   * was set to 0, preexisting buffers must be passed into the function. While
   * the underlying mechanism using raw bytes, a convenience function for
   * 2-byte samples is provided.
   */
  // For internally generated buffers for 8 to 16-bit samples.
  bool update();
  // Use when supplying your own buffers for 8-bit samples.
  bool update( uint8_t *txBuf, size_t *txCount, uint8_t *rxBuf, size_t rxCount );
  // Use when supplying your own buffers for 9 to 16-bit samples.
  bool update( uint16_t *txBuf, size_t *txCount, uint16_t *rxBuf, size_t rxCount ); 
  
  //setChannels( uint8_t channels );
  //uint8_t getChannels();
  //setBitDepth( uint8_t bits );
  //uint8_t getBitDepth();
  //setSampleRate( uint32_t rate );
  //uint8_t getSampleRate();
  
  // from Adafruit_USBD_Interface
  virtual uint16_t getInterfaceDescriptor(uint8_t itfnum_deprecated,
    uint8_t *buf, uint16_t bufsize);
};

#endif//USBD_AUDIO_H