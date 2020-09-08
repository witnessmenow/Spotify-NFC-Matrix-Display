/*******************************************************************
    Displays Album Art on an 64 x 64 RGB LED Matrix with an NFC
    Module to change what is played based on data on the NFC tag

    The library for the display will need to be modified to work
    with a 64x64 matrix:
    https://github.com/witnessmenow/ESP32-i2s-Matrix-Shield#using-a-64x64-display

    NOTE: You need to get a Refresh token to use this example
    Use the getRefreshToken example to get it.

    Parts:
    ESP32 D1 Mini stlye Dev board
        - Aliexpress*: http://s.click.aliexpress.com/e/C6ds4my
        - Amazon.com*: https://amzn.to/3gArkAY
        - Also available as an add-on with my Matrix Shield

    ESP32 I2S Matrix Shield
        - My Tindie: https://www.tindie.com/products/brianlough/esp32-i2s-matrix-shield/

    64 x 64 RGB LED Matrix
        - Aliexpress*: https://s.click.aliexpress.com/e/_BfjY0wfp

    PN532 NFC Module
        - Aliexpress*: https://s.click.aliexpress.com/e/_d7p8MoK
        - Amazon.co.uk*: https://amzn.to/2DnfvzY


 *  * = Affilate

    If you find what I do useful and would like to support me,
    please consider becoming a sponsor on Github
    https://github.com/sponsors/witnessmenow/


    Written by Brian Lough
    YouTube: https://www.youtube.com/brianlough
    Tindie: https://www.tindie.com/stores/brianlough/
    Twitter: https://twitter.com/witnessmenow
 *******************************************************************/


// ----------------------------
// Standard Libraries
// ----------------------------

#include <WiFi.h>
#include <WiFiClientSecure.h>

#define FS_NO_GLOBALS
#include <FS.h>
#include "SPIFFS.h"

#include <SPI.h>


// ----------------------------
// Additional Libraries - each one of these will need to be installed.
// ----------------------------

#define MATRIX_HEIGHT 64
#define MATRIX_WIDTH 64

#include <ESP32-RGB64x32MatrixPanel-I2S-DMA.h>
// This is the library for interfacing with the display

// Can be installed from the library manager (Search for "ESP32 64x32 LED MATRIX")
// https://github.com/mrfaptastic/ESP32-RGB64x32MatrixPanel-I2S-DMA

#include <ArduinoSpotify.h>
// Library for connecting to the Spotify API

// Install from Github
// https://github.com/witnessmenow/arduino-spotify-api

#include <ArduinoJson.h>
// Library used for parsing Json from the API responses

// Search for "Arduino Json" in the Arduino Library manager
// https://github.com/bblanchon/ArduinoJson

#include <JPEGDEC.h>
// Library for decoding Jpegs from the API responses

// Search for "JPEGDEC" in the Arduino Library manager
// https://github.com/bitbank2/JPEGDEC/

#include <NfcAdapter.h>
#include <PN532/PN532/PN532.h>
#include <PN532/PN532_SPI/PN532_SPI.h>
// Library for interfacing with the NFC Reader

// I modified the library to be able to use
// custom pins for the SPI

// Install from Github
// https://github.com/witnessmenow/Seeed_Arduino_NFC

//------- Replace the following! ------

char ssid[] = "SSID";         // your network SSID (name)
char password[] = "password"; // your network password
char clientId[] = "56t4373258u3405u43u543"; // Your client ID of your spotify APP
char clientSecret[] = "56t4373258u3405u43u543"; // Your client Secret of your spotify APP (Do Not share this!)

// Country code, including this is advisable
#define SPOTIFY_MARKET "IE"

#define SPOTIFY_REFRESH_TOKEN "AAAAAAAAAABBBBBBBBBBBCCCCCCCCCCCDDDDDDDDDDD"
//------- ---------------------- ------

//Wiring for the matrix can be found here:
// https://github.com/witnessmenow/ESP32-i2s-Matrix-Shield/blob/master/FAQ.md#what-pins-are-used-by-the-display

// SPI pins for the Module (i2c didn't work for me for some reason)
// If the module has a switch of jumpers to select the mode, make sure it's configured for SPI
#define NFC_SCLK 33
#define NFC_MISO 32
#define NFC_MOSI 21
#define NFC_SS 22

//------- ---------------------- ------

// including a "spotify_server_cert" variable
// header is included as part of the ArduinoSpotify libary
#include <ArduinoSpotifyCert.h>

// file name for where to save the image.
#define ALBUM_ART "/album.jpg"

// so we can compare and not download the same image if we already have it.
String lastAlbumArtUrl;

bool refreshArt = false;

WiFiClientSecure client;
ArduinoSpotify spotify(client, clientId, clientSecret, SPOTIFY_REFRESH_TOKEN);

PN532_SPI pn532spi(SPI, NFC_SS, NFC_SCLK, NFC_MISO, NFC_MOSI);
NfcAdapter nfc = NfcAdapter(pn532spi);

JPEGDEC jpeg;

// You might want to make this much smaller, so it will update responsively

unsigned long delayBetweenRequests = 5000; // Time between requests (5 seconds)
unsigned long requestDueTime;               //time when request due

unsigned long delayBetweenNfcReads = 200; // Time between NFC reads (.2 seconds)
unsigned long nfcDueTime;               //time when NFC read is due

RGB64x32MatrixPanel_I2S_DMA dma_display;

// This next function will be called during decoding of the jpeg file to
// render each block to the Matrix.  If you use a different display
// you will need to adapt this function to suit.
void JPEGDraw(JPEGDRAW *pDraw)
{
  // Stop further decoding as image is running off bottom of screen
  if (  pDraw->y >= dma_display.height() ) return;

  dma_display.drawRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
}

void setup() {

  Serial.begin(115200);

  // Initialise SPIFFS, if this fails try .begin(true)
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS initialisation failed!");
    while (1) yield(); // Stay here twiddling thumbs waiting
  }
  Serial.println("\r\nInitialisation done.");

  Serial.println("NDEF Reader");
  nfc.begin();
  if (nfc.fail) {
    // Could while loop here if you wanted
    Serial.println("NFC reader is not working!!!");
  }

  dma_display.begin();
  dma_display.fillScreen(dma_display.color565(0, 0, 0));

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  client.setCACert(spotify_server_cert);

  // If you want to enable some extra debugging
  // spotify._debug = true;
  Serial.println("Refreshing Access Tokens");
  if (!spotify.refreshAccessToken()) {
    Serial.println("Failed to get access tokens");
  }
}

fs::File myfile;

void * myOpen(const char *filename, int32_t *size) {
  myfile = SPIFFS.open(filename);
  *size = myfile.size();
  return &myfile;
}
void myClose(void *handle) {
  if (myfile) myfile.close();
}
int32_t myRead(JPEGFILE *handle, uint8_t *buffer, int32_t length) {
  if (!myfile) return 0;
  return myfile.read(buffer, length);
}
int32_t mySeek(JPEGFILE *handle, int32_t position) {
  if (!myfile) return 0;
  return myfile.seek(position);
}

int displayImage(char *albumArtUrl) {

  // In this example I reuse the same filename
  // over and over, maybe saving the art using
  // the album URI as the name would be better
  // as you could save having to download them each
  // time, but this seems to work fine.
  if (SPIFFS.exists(ALBUM_ART) == true) {
    Serial.println("Removing existing image");
    SPIFFS.remove(ALBUM_ART);
  }

  fs::File f = SPIFFS.open(ALBUM_ART, "w+");
  if (!f) {
    Serial.println("file open failed");
    return -1;
  }

  unsigned long lTime = millis();

  bool gotImage = spotify.getImage(albumArtUrl, &f);

  Serial.print("Time taken to get Image (ms): ");
  Serial.println(millis() - lTime);

  // Make sure to close the file!
  f.close();

  if (gotImage) {
    lTime = millis();
    jpeg.open((char *)ALBUM_ART, myOpen, myClose, myRead, mySeek, JPEGDraw);
    jpeg.decode(0, 0, 0);
    jpeg.close();
    Serial.print("Time taken to decode and display Image (ms): ");
    Serial.println(millis() - lTime);
    return 0;
  } else {
    return -2;
  }
}

void printCurrentlyPlayingToSerial(CurrentlyPlaying currentlyPlaying)
{
  if (!currentlyPlaying.error)
  {
    Serial.println("--------- Currently Playing ---------");


    Serial.print("Is Playing: ");
    if (currentlyPlaying.isPlaying)
    {
      Serial.println("Yes");
    } else {
      Serial.println("No");
    }

    Serial.print("Track: ");
    Serial.println(currentlyPlaying.trackName);
    Serial.print("Track URI: ");
    Serial.println(currentlyPlaying.trackUri);
    Serial.println();

    Serial.print("Artist: ");
    Serial.println(currentlyPlaying.firstArtistName);
    Serial.print("Artist URI: ");
    Serial.println(currentlyPlaying.firstArtistUri);
    Serial.println();

    Serial.print("Album: ");
    Serial.println(currentlyPlaying.albumName);
    Serial.print("Album URI: ");
    Serial.println(currentlyPlaying.albumUri);
    Serial.println();

    // will be in order of widest to narrowest
    // currentlyPlaying.numImages is the number of images that
    // are stored
    for (int i = 0; i < currentlyPlaying.numImages; i++) {
      Serial.println("------------------------");
      Serial.print("Album Image: ");
      Serial.println(currentlyPlaying.albumImages[i].url);
      Serial.print("Dimensions: ");
      Serial.print(currentlyPlaying.albumImages[i].width);
      Serial.print(" x ");
      Serial.print(currentlyPlaying.albumImages[i].height);
      Serial.println();
    }

    Serial.println("------------------------");
  }
}

bool updateSpotify(char *tagContent) {

  char body[200];

  // First let's check if we have a comma
  char *commaLocation = NULL;
  commaLocation = strchr (tagContent, ',');

  if (commaLocation != NULL) {
    // We have a comma, this means its a track with a context.

    uint8_t lengthOfString = strlen(tagContent);
    uint8_t contextIndex = commaLocation - tagContent + 1; //don't want the comma
    uint8_t contextLength = lengthOfString - contextIndex;
    char context[contextLength + 1];
    strncpy(context, commaLocation + 1, contextLength);
    context[contextLength] = '\0';

    Serial.print("context: ");
    Serial.println(context);

    uint8_t trackLength = commaLocation - tagContent;
    char track[trackLength + 1];
    strncpy(track, tagContent, trackLength);
    track[trackLength] = '\0';

    Serial.print("track: ");
    Serial.println(track);

    sprintf(body, "{\"context_uri\" : \"%s\", \"offset\": {\"uri\": \"%s\"}}", context, track);

  } else {
    char *isTrack = NULL;
    isTrack = strstr (tagContent, "track");
    if (isTrack) {
      Serial.print("track: ");
      Serial.println(tagContent);
      sprintf(body, "{\"uris\" : [\"%s\"]}", tagContent);
    } else {
      Serial.print("context: ");
      Serial.println(tagContent);
      sprintf(body, "{\"context_uri\" : \"%s\"}", tagContent);
    }
  }

  if (spotify.playAdvanced(body)) {
    Serial.println("done!");
    requestDueTime = 0; // force it to update
  }
}

void markDisplayAsTagRead() {
  dma_display.drawRect(1, 1, dma_display.width() - 2, dma_display.height() - 2, dma_display.color444(0, 0, 255));
  dma_display.drawRect(2, 2, dma_display.width() - 4, dma_display.height() - 4, dma_display.color444(255, 0, 0));
}

bool handleTag() {
  NfcTag tag = nfc.read();
  Serial.println(tag.getTagType());
  Serial.print("UID: "); Serial.println(tag.getUidString());

  if (tag.hasNdefMessage()) { // every tag won't have a message

    NdefMessage message = tag.getNdefMessage();
    Serial.print("\nThis NFC Tag contains an NDEF Message with ");
    Serial.print(message.getRecordCount());
    Serial.print(" NDEF Record");
    if (message.getRecordCount() != 1) {
      Serial.print("s");
    }
    Serial.println(".");

    // cycle through the records, printing some info from each
    int recordCount = message.getRecordCount();
    for (int i = 0; i < recordCount; i++) {
      Serial.print("\nNDEF Record "); Serial.println(i + 1);
      NdefRecord record = message.getRecord(i);
      // NdefRecord record = message[i]; // alternate syntax

      Serial.print("  TNF: "); Serial.println(record.getTnf());
      Serial.print("  Type: "); Serial.println(record.getType()); // will be "" for TNF_EMPTY

      // The TNF and Type should be used to determine how your application processes the payload
      // There's no generic processing for the payload, it's returned as a byte[]
      int payloadLength = record.getPayloadLength();
      byte payload[payloadLength];
      record.getPayload(payload);

      // Print the Hex and Printable Characters
      Serial.print("  Payload (HEX): ");
      PrintHexChar(payload, payloadLength);

      // id is probably blank and will return ""
      String uid = record.getId();
      if (uid != "") {
        Serial.print("  ID: "); Serial.println(uid);
      }

      // Force the data into a String (might work depending on the content)
      // Real code should use smarter processing
      char payloadAsString[payloadLength + 1];
      int numChars = 0;
      for (int c = 0; c < payloadLength; c++) {
        if ((char)payload[c] != '\0') {
          payloadAsString[numChars] = (char)payload[c];
          numChars++;
        }
      }

      payloadAsString[numChars] = '\0';
      markDisplayAsTagRead();
      refreshArt = true; // update the art to remove the mark, even if the art doesnt change.
      return updateSpotify(payloadAsString);
    }
  }

  return false;
}

void loop() {
  if (millis() > requestDueTime)
  {
    Serial.print("Free Heap: ");
    Serial.println(ESP.getFreeHeap());

    Serial.println("getting currently playing song:");
    // Market can be excluded if you want e.g. spotify.getCurrentlyPlaying()
    CurrentlyPlaying currentlyPlaying = spotify.getCurrentlyPlaying(SPOTIFY_MARKET);
    if (!currentlyPlaying.error)
    {
      printCurrentlyPlayingToSerial(currentlyPlaying);

      // Smallest (narrowest) image will always be last.
      SpotifyImage smallestImage = currentlyPlaying.albumImages[currentlyPlaying.numImages - 1];
      String newAlbum = String(smallestImage.url);
      if (newAlbum != lastAlbumArtUrl) {
        Serial.println("Updating Art");
        int displayImageResult = displayImage(smallestImage.url);
        if (displayImageResult == 0) {
          lastAlbumArtUrl = newAlbum;
        } else {
          Serial.print("failed to display image: ");
          Serial.println(displayImageResult);
        }
      } else if (refreshArt) {
        refreshArt = false;
        jpeg.open((char *)ALBUM_ART, myOpen, myClose, myRead, mySeek, JPEGDraw);
        jpeg.decode(0, 0, 0);
        jpeg.close();
      }
    }

    requestDueTime = millis() + delayBetweenRequests;
  }

  if (millis() > nfcDueTime)
  {
    if (nfc.tagPresent() && handleTag()) {
      nfcDueTime = millis() + 5000; // 5 second cool down on NFC tag if succesful
    } else {
      nfcDueTime = millis() + delayBetweenNfcReads;
    }
  }

}
