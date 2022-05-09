# Spotify-NFC-Matrix-Display

A display built using an RGB Matrix panel to display the live album art of what song is currently playing. Can also change what is being played using NFC tags

![image](https://user-images.githubusercontent.com/1562562/129483616-5283837b-0927-497f-b159-4ebeda6b0761.png)

WIP project

## Help Support what I do!

[If you enjoy my work, please consider becoming a Github sponsor!](https://github.com/sponsors/witnessmenow/)

### Dependant Libraries

The following libraries need to be installed for this sketch to work:

| Library Name/Link                                                                                 | Purpose                                     | Library manager          |
| ------------------------------------------------------------------------------------------------- | ------------------------------------------- | ------------------------ |
| (ESP32-HUB75-MatrixPanel-I2S-DMA)[https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-I2S-DMA] | For controlling the LED Matrix              | Yes ("ESP32 MATRIX DMA") |
| (SpotifyArduino)[https://github.com/witnessmenow/spotify-api-arduino]                             | For interacting with Spotify API            | No                       |
| (ArduinoJson)[https://github.com/bblanchon/ArduinoJson]                                           | For controlling the LED Matrix              | Yes ("Arduino Json")     |
| (NfcAdapter)[https://github.com/witnessmenow/Seeed_Arduino_NFC]                                   | For NFC device (this is a modified version) | No                       |
