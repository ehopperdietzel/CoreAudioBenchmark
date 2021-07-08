#include <AudioToolbox/AudioToolbox.h>
#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <chrono>

int NUM_TRACKS;
int BUFFER_SIZE;
int SAMPLE_RATE;
int BIT_DEPTH;

// Componentes para instanciar AudioUnits
AudioComponent outputComponent, mixerComponent, fileComponent, eqComponent, compressorComponent, reverbComponent;

// Audio Units
AudioUnit mixerAU,outputAU;
AudioUnit *eqAUs;
AudioUnit *fileAUs;
AudioUnit *compressorAUs;
AudioUnit *reverbAUs;

char fileName[128];
bool isPar = true;

float volume = 0.0f;

int callbacksCount = 0;
double delayAvg = 0.0;

void S(OSStatus err)
{
  if(err != noErr)
    printf("%s","Error\n");
}

void finish(int res)
{
  AudioOutputUnitStop(outputAU);
  AudioUnitUninitialize(mixerAU);
  AudioUnitUninitialize(outputAU);

  for(int i = 0; i < NUM_TRACKS; i++)
  {
    AudioUnitUninitialize(fileAUs[i]);
    AudioUnitUninitialize(eqAUs[i]);
    AudioUnitUninitialize(compressorAUs[i]);
    AudioUnitUninitialize(reverbAUs[i]);

    AudioComponentInstanceDispose(fileAUs[i]);
    AudioComponentInstanceDispose(eqAUs[i]);
    AudioComponentInstanceDispose(compressorAUs[i]);
    AudioComponentInstanceDispose(reverbAUs[i]);
  }

  AudioComponentInstanceDispose(mixerAU);
  AudioComponentInstanceDispose(outputAU);

  delete[] eqAUs;
  delete[] fileAUs;
  delete[] compressorAUs;
  delete[] reverbAUs;

  exit(res);
}
void configureDevice()
{
  AudioObjectID DeviceAudioObjectID;
  AudioObjectPropertyAddress DevicePropertyAddress;
  UInt32 AudioDeviceQuerySize;
  OSStatus Status;
  int BufferSize = 0;

  //Get Audio Device ID
  DevicePropertyAddress.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
  DevicePropertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
  DevicePropertyAddress.mElement = 0;
  AudioDeviceQuerySize = sizeof(AudioDeviceID);
  S(AudioObjectGetPropertyData(kAudioObjectSystemObject, &DevicePropertyAddress, 0, nullptr, &AudioDeviceQuerySize, &DeviceAudioObjectID));

  DevicePropertyAddress.mSelector = kAudioDevicePropertyBufferFrameSizeRange;
  DevicePropertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
  DevicePropertyAddress.mElement = kAudioObjectPropertyElementMaster;
  AudioValueRange BufferSizeRange = { 0, 0 };
  AudioDeviceQuerySize = sizeof(AudioValueRange);
  S(AudioObjectGetPropertyData(DeviceAudioObjectID, &DevicePropertyAddress, 0, nullptr, &AudioDeviceQuerySize, &BufferSizeRange));

  AudioDeviceQuerySize = sizeof(CFStringRef);
  CFStringRef theCFString = NULL;
  DevicePropertyAddress.mSelector = kAudioDevicePropertyDeviceUID;
  DevicePropertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
  DevicePropertyAddress.mElement = 0;

  S(AudioObjectGetPropertyData(DeviceAudioObjectID,&DevicePropertyAddress, 0,nullptr, &AudioDeviceQuerySize, &theCFString));

  char name[128];
  CFStringGetCString(theCFString, name, sizeof(name), kCFStringEncodingUTF8);
  //printf("Min:%f Max:%f\n",BufferSizeRange.mMinimum,BufferSizeRange.mMaximum);
  //printf("Device Name:%s\n",name);

  UInt32 frames = BUFFER_SIZE;
  AudioDeviceQuerySize = sizeof(UInt32);
  DevicePropertyAddress.mSelector = kAudioDevicePropertyBufferFrameSize;
  DevicePropertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
  DevicePropertyAddress.mElement = 0;
  AudioObjectSetPropertyData(DeviceAudioObjectID, &DevicePropertyAddress, 0, nullptr, AudioDeviceQuerySize, &frames);


  AudioDeviceQuerySize = sizeof(UInt32);
  DevicePropertyAddress.mSelector = kAudioDevicePropertyBufferFrameSize;
  DevicePropertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
  DevicePropertyAddress.mElement = 0;

  S(AudioObjectGetPropertyData(DeviceAudioObjectID,&DevicePropertyAddress, 0,nullptr, &AudioDeviceQuerySize, &frames));
  //printf("Frames:%u\n",frames);


  AudioValueRange inputSampleRate;
  inputSampleRate.mMinimum = SAMPLE_RATE;
  inputSampleRate.mMaximum = SAMPLE_RATE;
  DevicePropertyAddress.mSelector = kAudioDevicePropertyNominalSampleRate;
  DevicePropertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
  DevicePropertyAddress.mElement = 0;
  S(AudioObjectSetPropertyData(DeviceAudioObjectID,
                              &DevicePropertyAddress,
                              0,
                              nullptr,
                              sizeof(inputSampleRate),
                              &inputSampleRate));


}

// Genera los AudioComponents
void genComponents()
{
  // Componente de Salida
  AudioComponentDescription AUDesc;
  AUDesc.componentType = kAudioUnitType_Output;
  AUDesc.componentSubType = kAudioUnitSubType_DefaultOutput;
  AUDesc.componentManufacturer = kAudioUnitManufacturer_Apple;
  AUDesc.componentFlags = 0;
  AUDesc.componentFlagsMask = 0;
  outputComponent = AudioComponentFindNext(NULL, &AUDesc);

  // Componente mixer
  AUDesc.componentType = kAudioUnitType_Mixer;
  AUDesc.componentSubType = kAudioUnitSubType_StereoMixer;
  mixerComponent = AudioComponentFindNext(NULL, &AUDesc);

  // Componente filePlayer
  AUDesc.componentType = kAudioUnitType_Generator;
  AUDesc.componentSubType = kAudioUnitSubType_AudioFilePlayer;
  fileComponent = AudioComponentFindNext(NULL, &AUDesc);

  // Componente equalizador
  AUDesc.componentType = kAudioUnitType_Effect;
  AUDesc.componentSubType = kAudioUnitSubType_NBandEQ;
  eqComponent = AudioComponentFindNext(NULL, &AUDesc);

  // Componente compresor
  AUDesc.componentType = kAudioUnitType_Effect;
  AUDesc.componentSubType = kAudioUnitSubType_DynamicsProcessor;
  compressorComponent = AudioComponentFindNext(NULL, &AUDesc);

  // Componente reverb
  AUDesc.componentType = kAudioUnitType_Effect;
  AUDesc.componentSubType = kAudioUnitSubType_MatrixReverb;
  reverbComponent = AudioComponentFindNext(NULL, &AUDesc);


}

std::chrono::duration<double> diff;
std::chrono::time_point<std::chrono::high_resolution_clock> start;
std::chrono::time_point<std::chrono::high_resolution_clock> end;

OSStatus callback(
  void *inRefCon,
  AudioUnitRenderActionFlags *ioActionFlags,
  const AudioTimeStamp *inTimeStamp,
  UInt32 inBusNumber,
  UInt32 inNumberFrames,
  AudioBufferList * ioData)
{
    OSStatus err = noErr;
    start = std::chrono::high_resolution_clock::now();
    err = AudioUnitRender(mixerAU,ioActionFlags,inTimeStamp,inBusNumber,inNumberFrames,ioData);
    callbacksCount++;
    end = std::chrono::high_resolution_clock::now();
    diff = end-start;
    delayAvg += diff.count();

    if(callbacksCount == 50)
    {
      //printf("Delay AVG:%f\n",delayAvg/double(callbacksCount));
      if(delayAvg/double(callbacksCount) < double(BUFFER_SIZE)/double(SAMPLE_RATE))
      {
        //printf("Result: Successful\n");
        finish(0);
      }
      else
      {
        //printf("Result: Unsuccessful\n");
        finish(1);
      }
    }

    return err;
}

// Configura la AU de salida
void setupOutputAU()
{
  AudioComponentInstanceNew(outputComponent, &outputAU);



  AudioUnitInitialize(outputAU);

}

void setupMixerAU()
{
  AudioComponentInstanceNew(mixerComponent, &mixerAU);
  AudioUnitInitialize(mixerAU);
}

void setupFileAU(AudioUnit *fileAU,AudioUnit *eqAU,AudioUnit *compressorAU,AudioUnit *reverbAU,int index)
{
  AudioFileID fileID;
  AudioStreamBasicDescription fileFormat;

  AudioComponentInstanceNew(fileComponent,fileAU);
  AudioUnitInitialize(*fileAU);

  sprintf(fileName,"./%i_%i.wav",SAMPLE_RATE,BIT_DEPTH);

  // Url de la cancion
  CFURLRef inputFileURL = CFURLCreateWithFileSystemPath( kCFAllocatorDefault,CFStringCreateWithCString(NULL, fileName, kCFStringEncodingUTF8), kCFURLPOSIXPathStyle, false);


  AudioFileOpenURL(inputFileURL, kAudioFileReadPermission,0,&fileID);

  // Elimina el url
  CFRelease(inputFileURL);

  UInt32 propSize = sizeof(fileFormat);
  AudioFileGetProperty(fileID,kAudioFilePropertyDataFormat, &propSize, &fileFormat);

  // Asigna el archivo
  AudioUnitSetProperty(*fileAU,kAudioUnitProperty_ScheduledFileIDs, kAudioUnitScope_Global,0, &fileID, sizeof(fileID));

  // Lee el número de frames
  UInt64 nPackets;
  UInt32 propsize = sizeof(nPackets);
  AudioFileGetProperty(fileID,kAudioFilePropertyAudioDataPacketCount,&propsize,&nPackets);

  //printf("%llu",nPackets);

  // Configura la región de audio que se reproducirá
  ScheduledAudioFileRegion rgn;
  memset (&rgn.mTimeStamp, 0, sizeof(rgn.mTimeStamp));
  rgn.mTimeStamp.mFlags = kAudioTimeStampSampleTimeValid;
  rgn.mTimeStamp.mSampleTime = 0;
  rgn.mCompletionProc = NULL;
  rgn.mCompletionProcUserData = NULL;
  rgn.mAudioFile = fileID;
  rgn.mLoopCount = 0;
  rgn.mStartFrame = 0;
  rgn.mFramesToPlay = nPackets * fileFormat.mFramesPerPacket;
  AudioUnitSetProperty(*fileAU, kAudioUnitProperty_ScheduledFileRegion,kAudioUnitScope_Global, 0,&rgn,sizeof(rgn));

  // Indica en que momento debe comenzar a reproducir
  AudioTimeStamp startTime;
  memset (&startTime, 0, sizeof(startTime));
  startTime.mFlags = kAudioTimeStampSampleTimeValid;
  startTime.mSampleTime = -1;
  AudioUnitSetProperty(*fileAU, kAudioUnitProperty_ScheduleStartTimeStamp,kAudioUnitScope_Global, 0,&startTime, sizeof(startTime));


  // EQ
  AudioComponentInstanceNew(eqComponent,eqAU);
  AudioUnitInitialize(*eqAU);

  UInt32 bands = 8;
  AudioUnitSetProperty(
      *eqAU,
      kAUNBandEQProperty_NumberOfBands,
      kAudioUnitScope_Global,
      0,
      &bands,
      sizeof(bands)
  );

  int bandTypes[8] = {
    kAUNBandEQFilterType_2ndOrderButterworthHighPass,
    kAUNBandEQFilterType_LowShelf,
    kAUNBandEQFilterType_Parametric,
    kAUNBandEQFilterType_Parametric,
    kAUNBandEQFilterType_Parametric,
    kAUNBandEQFilterType_Parametric,
    kAUNBandEQFilterType_HighShelf,
    kAUNBandEQFilterType_2ndOrderButterworthLowPass
  };

  for(int i = 0; i < 8; i++)
  {
    AudioUnitSetParameter(
        *eqAU,
        kAUNBandEQParam_BypassBand+i,
        kAudioUnitScope_Global,
        0,
        0,
        0
    );

    AudioUnitSetParameter(
        *eqAU,
        kAUNBandEQParam_FilterType+i,
        kAudioUnitScope_Global,
        0,
        bandTypes[i],
        0
    );
  }

  AudioUnitSetParameter(
      *eqAU,
      kAUNBandEQParam_Frequency,
      kAudioUnitScope_Global,
      0,
      5000,
      0
  );

  // Compresor
  AudioComponentInstanceNew(compressorComponent,compressorAU);
  AudioUnitInitialize(*compressorAU);

  if(isPar)
  {
    // Reverb
    AudioComponentInstanceNew(reverbComponent,reverbAU);
    AudioUnitInitialize(*reverbAU);
  }


  // Filer -> eq
  AudioUnitConnection connection;
  connection.sourceAudioUnit = *fileAU;
  connection.sourceOutputNumber = 0;
  connection.destInputNumber = 0;

  AudioUnitSetProperty(
      *eqAU,
      kAudioUnitProperty_MakeConnection,
      kAudioUnitScope_Input,
      index,
      &connection,
      sizeof(connection)
  );

  // EQ -> compressor
  connection.sourceAudioUnit = *eqAU;
  connection.sourceOutputNumber = 0;
  connection.destInputNumber = 0;


  AudioUnitSetProperty(
      *compressorAU,
      kAudioUnitProperty_MakeConnection,
      kAudioUnitScope_Input,
      0,
      &connection,
      sizeof(connection)
  );


  // compressor -> reverb
  connection.sourceAudioUnit = *compressorAU;
  connection.sourceOutputNumber = 0;
  connection.destInputNumber = 0;


  if(isPar)
  {
    AudioUnitSetProperty(
        *reverbAU,
        kAudioUnitProperty_MakeConnection,
        kAudioUnitScope_Input,
        0,
        &connection,
        sizeof(connection)
    );

    // reverb -> mixer
    connection.sourceAudioUnit = *reverbAU;
    connection.sourceOutputNumber = 0;
    connection.destInputNumber = index;


    AudioUnitSetProperty(
        mixerAU,
        kAudioUnitProperty_MakeConnection,
        kAudioUnitScope_Input,
        index,
        &connection,
        sizeof(connection)
    );
  }
  else
  {
    AudioUnitSetProperty(
        mixerAU,
        kAudioUnitProperty_MakeConnection,
        kAudioUnitScope_Input,
        0,
        &connection,
        sizeof(connection)
    );
  }


  isPar = true;//!isPar;


  // Asigna volumen
  AudioUnitSetParameter(
      mixerAU,
      kStereoMixerParam_Volume,
      kAudioUnitScope_Input,
      index,
      volume,
      0
  );
}

void connectAU()
{
/*
  // Mixer -> Output
  AudioUnitConnection connection2;
  connection2.sourceAudioUnit = mixerAU;
  connection2.sourceOutputNumber = 0;
  connection2.destInputNumber = 0;

  AudioUnitSetProperty(
      outputAU,
      kAudioUnitProperty_MakeConnection,
      kAudioUnitScope_Input,
      0,
      &connection2,
      sizeof(connection2)
  );
  */
  AURenderCallbackStruct input;
  input.inputProc = callback;
  input.inputProcRefCon = 0;

  AudioUnitSetProperty(
          outputAU,
          kAudioUnitProperty_SetRenderCallback,
          kAudioUnitScope_Input,
          0,
          &input,
          sizeof(input));

}

void play()
{
  AudioOutputUnitStart(outputAU);
}


// ./benchmark pistas buffersize samplerate bitdepth
int main(int argc,char **argv)
{
  NUM_TRACKS = atoi(argv[1]);
  BUFFER_SIZE = atoi(argv[2]);
  SAMPLE_RATE = atoi(argv[3]);
  BIT_DEPTH = atoi(argv[4]);

  //printf("Pistas:%i - BufferSize:%i\n",NUM_TRACKS,BUFFER_SIZE);

  eqAUs = new AudioUnit[NUM_TRACKS];
  fileAUs = new AudioUnit[NUM_TRACKS];
  compressorAUs = new AudioUnit[NUM_TRACKS];
  reverbAUs = new AudioUnit[NUM_TRACKS];

  configureDevice();
  genComponents();
  setupOutputAU();
  setupMixerAU();

  for(int i = 0; i < NUM_TRACKS; i++)
  {
    setupFileAU(&fileAUs[i],&eqAUs[i],&compressorAUs[i],&reverbAUs[i],i);
  }

  //printf("Periodo:%f\n",double(BUFFER_SIZE)/double(SAMPLE_RATE));
  connectAU();



  play();

  while(true)
  {

  }
  return 0;
}
