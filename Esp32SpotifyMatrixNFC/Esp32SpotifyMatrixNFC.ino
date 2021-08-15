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

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
// This is the library for interfacing with the display

// Can be installed from the library manager (Search for "ESP32 MATRIX DMA")
// https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-I2S-DMA

#include <SpotifyArduino.h>
// Library for connecting to the Spotify API

// Install from Github
// https://github.com/witnessmenow/spotify-api-arduino

// including a "spotify_server_cert" variable
// header is included as part of the SpotifyArduino libary
#include <SpotifyArduinoCert.h>


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

// I modified the library to so it wouldn't lock if the NFC
// reader wasn't present

// Install from Github
// https://github.com/witnessmenow/Seeed_Arduino_NFC

// ----------------------------
// Dependency Libraries - each one of these will need to be installed.
// ----------------------------

// Adafruit GFX library is a dependency for the matrix Library
// Can be installed from the library manager
// https://github.com/adafruit/Adafruit-GFX-Library

// -------------------------------------
// -------   Matrix Config   ------
// -------------------------------------

#define PANEL_RES_X 64      // Number of pixels wide of each INDIVIDUAL panel module. 
#define PANEL_RES_Y 64     // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 1      // Total number of panels chained one to another

// -------------------------------------
// -------   NFC Pin Config   ------
// -------------------------------------

//Wiring for the matrix can be found here:
// https://github.com/witnessmenow/ESP32-i2s-Matrix-Shield/blob/master/FAQ.md#what-pins-are-used-by-the-display

// SPI pins for the Module (i2c didn't work for me for some reason)
// If the module has a switch of jumpers to select the mode, make sure it's configured for SPI
#define NFC_SCLK 33
#define NFC_MISO 32
#define NFC_MOSI 21
#define NFC_SS 22


// --------------------------------------------------
// -------   General Config (Replace these!)   ------
// --------------------------------------------------

char ssid[] = "SSID";         // your network SSID (name)
char password[] = "password"; // your network password

char clientId[] = "56t4373258u3405u43u543";     // Your client ID of your spotify APP
char clientSecret[] = "56t4373258u3405u43u543"; // Your client Secret of your spotify APP (Do Not share this!)

// Country code, including this is advisable
#define SPOTIFY_MARKET "IE"

#define SPOTIFY_REFRESH_TOKEN "AAAAAAAAAABBBBBBBBBBBCCCCCCCCCCCDDDDDDDDDDD"
//------- ---------------------- ------

// file name for where to save the image.
#define ALBUM_ART "/album.jpg"

// so we can compare and not download the same image if we already have it.
String albumArtUrl;

String newAlbumArtUrl;

bool refreshArt = false;

WiFiClientSecure client;
SpotifyArduino spotify(client, clientId, clientSecret, SPOTIFY_REFRESH_TOKEN);


//The matrix uses the default SPI pins, so we need to use custom ones
SPIClass spi = SPIClass(HSPI);
//spi.begin(NFC_SCLK, NFC_MISO, NFC_MOSI, NFC_SS);

PN532_SPI pn532spi(spi, NFC_SS);
NfcAdapter nfc = NfcAdapter(pn532spi);

// You might want to make this much smaller, so it will update responsively

unsigned long delayBetweenRequests = 5000; // Time between requests (5 seconds)
unsigned long requestDueTime;               //time when request due

unsigned long delayBetweenNfcReads = 200; // Time between NFC reads (.2 seconds)
unsigned long nfcDueTime;               //time when NFC read is due

MatrixPanel_I2S_DMA *dma_display = nullptr;

JPEGDEC jpeg;

// This next function will be called during decoding of the jpeg file to
// render each block to the Matrix.  If you use a different display
// you will need to adapt this function to suit.
void JPEGDraw(JPEGDRAW *pDraw)
{
  // Stop further decoding as image is running off bottom of screen
  if (  pDraw->y >= dma_display->height() ) return;

  dma_display->drawRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
}

void configDisplay() {
  HUB75_I2S_CFG mxconfig(
    PANEL_RES_X,   // module width
    PANEL_RES_Y,   // module height
    PANEL_CHAIN    // Chain length
  );

  mxconfig.gpio.e = 18;

  // May or may not be needed depending on your matrix
  // Example of what needing it looks like:
  // https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-I2S-DMA/issues/134#issuecomment-866367216
  mxconfig.clkphase = false;

  //mxconfig.driver = HUB75_I2S_CFG::FM6126A;

  // Display Setup
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
}

void setup() {

  Serial.begin(115200);

  configDisplay();
  dma_display->fillScreen(dma_display->color565(0, 0, 0));

  dma_display->setTextSize(1);     // size 1 == 8 pixels high
  dma_display->setTextWrap(false); // Don't wrap at end of line - will do ourselves
  dma_display->setTextColor(dma_display->color565(255, 0, 255));

  dma_display->setCursor(1, 0);    // start at top left, with 8 pixel of spacing

  dma_display->print("SPIFFS: ");
  dma_display->setCursor(48, 0);
  // Initialise SPIFFS, if this fails try .begin(true)
  if (!SPIFFS.begin()) {
    dma_display->print("NO!");
    Serial.println("SPIFFS initialisation failed!");
    while (1) yield(); // Stay here twiddling thumbs waiting
  }
  Serial.println("\r\nInitialisation done.");
  dma_display->print("OK!");

  Serial.println("NDEF Reader");
  dma_display->setCursor(1, 10);    // start at top left, with 8 pixel of spacing
  dma_display->print("NFC: ");
  dma_display->setCursor(48, 10);

  //The matrix uses the default SPI pins, so we need to use custom ones
  spi.begin(NFC_SCLK, NFC_MISO, NFC_MOSI, NFC_SS);
  nfc.begin();
  if (nfc.fail) {
    // Could while loop here if you wanted
    dma_display->print("NO!");
    Serial.println("NFC reader is not working!!!");
  } else {
    dma_display->print("OK!");
  }

  dma_display->setCursor(1, 20);    // start at top left, with 8 pixel of spacing
  dma_display->print("WiFi: ");
  dma_display->setCursor(48, 20);
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

  dma_display->print("OK!");

  client.setCACert(spotify_server_cert);

  dma_display->setCursor(1, 30);    // start at top left, with 8 pixel of spacing
  dma_display->print("Spotify: ");
  dma_display->setCursor(48, 30);
  Serial.println("Refreshing Access Tokens");
  while (!spotify.refreshAccessToken()) {
    Serial.println("Failed to get access tokens, will try again");
    delay(1000);
  }

  dma_display->print("OK!");

  delay(1000);
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

void drawImageFile(char *imageFileUri) {
  unsigned long lTime = millis();
  lTime = millis();
  jpeg.open(imageFileUri, myOpen, myClose, myRead, mySeek, JPEGDraw);
  jpeg.decode(0, 0, 0);
  jpeg.close();
  Serial.print("Time taken to decode and display Image (ms): ");
  Serial.println(millis() - lTime);
}

int displayImage(char* albumArtUrl) {

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
    drawImageFile((char *)ALBUM_ART);
    return 0;
  } else {
    return -2;
  }
}

void printCurrentlyPlayingToSerial(CurrentlyPlaying currentlyPlaying)
{
  // Use the details in this method or if you want to store them
  // make sure you copy them (using something like strncpy)
  // const char* artist =

  Serial.println("--------- Currently Playing ---------");

  Serial.print("Is Playing: ");
  if (currentlyPlaying.isPlaying)
  {
    Serial.println("Yes");
  }
  else
  {
    Serial.println("No");
  }

  Serial.print("Track: ");
  Serial.println(currentlyPlaying.trackName);
  Serial.print("Track URI: ");
  Serial.println(currentlyPlaying.trackUri);
  Serial.println();

  Serial.println("Artists: ");
  for (int i = 0; i < currentlyPlaying.numArtists; i++)
  {
    Serial.print("Name: ");
    Serial.println(currentlyPlaying.artists[i].artistName);
    Serial.print("Artist URI: ");
    Serial.println(currentlyPlaying.artists[i].artistUri);
    Serial.println();
  }

  Serial.print("Album: ");
  Serial.println(currentlyPlaying.albumName);
  Serial.print("Album URI: ");
  Serial.println(currentlyPlaying.albumUri);
  Serial.println();

  long progress = currentlyPlaying.progressMs; // duration passed in the song
  long duration = currentlyPlaying.durationMs; // Length of Song
  Serial.print("Elapsed time of song (ms): ");
  Serial.print(progress);
  Serial.print(" of ");
  Serial.println(duration);
  Serial.println();

  float percentage = ((float)progress / (float)duration) * 100;
  int clampedPercentage = (int)percentage;
  Serial.print("<");
  for (int j = 0; j < 50; j++)
  {
    if (clampedPercentage >= (j * 2))
    {
      Serial.print("=");
    }
    else
    {
      Serial.print("-");
    }
  }
  Serial.println(">");
  Serial.println();

  // will be in order of widest to narrowest
  // currentlyPlaying.numImages is the number of images that
  // are stored
  for (int i = 0; i < currentlyPlaying.numImages; i++)
  {
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

void currentlyPlayingCallback(CurrentlyPlaying currentlyPlaying) {
  printCurrentlyPlayingToSerial(currentlyPlaying);

  // Smallest (narrowest) image will always be last.
  SpotifyImage smallestImage = currentlyPlaying.albumImages[currentlyPlaying.numImages - 1];
  newAlbumArtUrl = String(smallestImage.url);

}

bool handleSpotifyUrl(char *tagContent) {
  char body[200];
  char contextUri[50];
  char trackUri[50];

  bool isTrack = false;

  //open.spotify.com/album/47lgREYotnsiuddvu6dXlk?si=F0r50tIETo-BffEB-HSpng&utm_source=copy-link&dl_branch=1
  // Note: the "https://" is stripped by the tag
  if (strncmp(tagContent, "open.spotify.com/album/", 23) == 0) {
    sprintf(contextUri, "spotify:album:%.*s", 22, tagContent + 23); // 22 is length of code, 23 is offset to get to the code (47lgREYotnsiuddvu6dXlk in above example)
  } else if (strncmp(tagContent, "open.spotify.com/playlist/", 26) == 0) {
    sprintf(contextUri, "spotify:playlist:%.*s", 22, tagContent + 26);
  } else if (strncmp(tagContent, "open.spotify.com/track/", 23) == 0) {
    isTrack = true;
    sprintf(trackUri, "spotify:track:%.*s", 22, tagContent + 23);
  } else {
    Serial.print("Unknown URL: ");
    Serial.println(tagContent);
    return false;
  }

  if (isTrack) {
    Serial.print("track: ");
    Serial.println(tagContent);
    sprintf(body, "{\"uris\" : [\"%s\"]}", trackUri);
  } else {
    Serial.print("context: ");
    Serial.println(tagContent);
    sprintf(body, "{\"context_uri\" : \"%s\"}", contextUri);
  }

  if (spotify.playAdvanced(body)) {
    Serial.println("done!");
    requestDueTime = 0; // force it to update
  }
}

bool handleSpotifyUri(char *tagContent) {
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

bool updateSpotify(char *tagContent) {

  if (strncmp(tagContent+1, "open.spotify.com", 16) == 0) { // The +1 is cause first charcter indicated protocol (I think), not needed anyways
    handleSpotifyUrl(tagContent + 1);
  } else if (strncmp(tagContent + 8, "open.spotify.com", 16) == 0) { // In case it's written as plain text, skipping the "https://"
    handleSpotifyUrl(tagContent + 8);
  } else if (strncmp(tagContent, "spotify:", 8) == 0) {
    // Probably in the format: spotify:track:4mCsFkDzm6z8j0glKdE164
    handleSpotifyUri(tagContent);
  } else {
    // Not reconginized format.
    // Should maybe flash a square or something
    refreshArt = true;
  }
}

void markDisplayAsTagRead() {
  dma_display->drawRect(1, 1, dma_display->width() - 2, dma_display->height() - 2, dma_display->color444(0, 0, 255));
  dma_display->drawRect(2, 2, dma_display->width() - 4, dma_display->height() - 4, dma_display->color444(255, 0, 0));
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
      Serial.print("  Payload (String): ");
      Serial.println(payloadAsString);
      return updateSpotify(payloadAsString );
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
    bool updatedDisplay = false;
    int status = spotify.getCurrentlyPlaying(currentlyPlayingCallback, SPOTIFY_MARKET);
    if (status == 200)
    {
      Serial.println("Successfully got currently playing");
      if (newAlbumArtUrl != albumArtUrl) {
        Serial.println("Fetching album Art");
        Serial.println((char*) albumArtUrl.c_str());
        int displayImageResult = displayImage((char*) newAlbumArtUrl.c_str());
        if (displayImageResult == 0) {
          albumArtUrl = newAlbumArtUrl;
          refreshArt = false; // if it was set to refresh before, it would have been for the old artwork
        } else {
          Serial.print("failed to display image: ");
          Serial.println(displayImageResult);
        }
      }
    }
    else if (status == 204)
    {
      Serial.println("Doesn't seem to be anything playing");
      albumArtUrl = "";
      dma_display->fillScreen(dma_display->color565(0, 0, 0));
    }
    else
    {
      Serial.print("Error: ");
      Serial.println(status);
    }

    if (refreshArt) {
      refreshArt = false;
      drawImageFile((char *)ALBUM_ART);
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
