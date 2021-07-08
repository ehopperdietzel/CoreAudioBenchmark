/**********************************
 **
 ** ./benchmark buffersize samplerate bitdepth
 **
 **********************************/

#include <AudioToolbox/AudioToolbox.h>
#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <chrono>
#include <sys/resource.h>
#include <sys/time.h>

#define EQ_BANDS 16
#define MAX_TRACKS 128

// Número de pistas de audio, se irán incrementando y midiendo el rendimiento en cada caso
int NUM_TRACKS = 1;

// Parámetros de configuración dados al ejecutar el programa
int BUFFER_SIZE,SAMPLE_RATE,BIT_DEPTH;

// Componentes para instanciar AudioUnits
AudioComponent outputComponent, mixerComponent, fileComponent, eqComponent, compressorComponent, reverbComponent;

// Audio Units ( Asignamos un máximo de 256 ya que es el número máximo de file descriptors que permite Mac OS X por defecto )
AudioUnit mixerAU,outputAU;
AudioUnit eqAUs[MAX_TRACKS];
AudioUnit fileAUs[MAX_TRACKS];
AudioUnit compressorAUs[MAX_TRACKS];
AudioUnit reverbAUs[MAX_TRACKS];

// Variables para medir el tiempo de procesamiento en cada callback
std::chrono::duration<double> diff;
std::chrono::time_point<std::chrono::high_resolution_clock> start;
std::chrono::time_point<std::chrono::high_resolution_clock> end;
struct timeval wall_start;
struct timeval wall_now;
struct rusage usage_start;
struct rusage usage_now;

// Indica si se está procesando audio
bool playing = false;
bool success = true;

char fileName[128];

float volume = 0.5f;

int callbacksCount = 0;
double delayAvg = 0.0;
double delayMax = 0.0;
double delayMin = 0.0;
double delayLimit;

void S(OSStatus err)
{
  if(err != noErr)
    printf("%s","Error\n");
}

// Inicia el procesamiento de audio
void play()
{
  callbacksCount = 0;
  delayAvg = 0.0;
  delayMax = 0.0;
  delayMin = 100000000.0;
  playing = true;
  success = true;
  AudioOutputUnitStart(outputAU);
}

// Detiene el procesamiento de audio
void stop()
{
  AudioOutputUnitStop(outputAU);
  playing = false;
}

// Termina el benchmark
void finish()
{
  AudioOutputUnitStop(outputAU);
  AudioUnitUninitialize(mixerAU);
  AudioUnitUninitialize(outputAU);

  for(int i = 0; i < MAX_TRACKS; i++)
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

  printf("\n\nBenchmark finalizado con éxito.");

  exit(0);
}

// Configura el dispositivo de salida
void configureDevice()
{
  AudioObjectID DeviceAudioObjectID;
  AudioObjectPropertyAddress DevicePropertyAddress;
  UInt32 AudioDeviceQuerySize;
  OSStatus Status;
  int BufferSize = 0;

  // Obtiene el ID del dispositivo de audio configurado en el sistema
  DevicePropertyAddress.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
  DevicePropertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
  DevicePropertyAddress.mElement = 0;
  AudioDeviceQuerySize = sizeof(AudioDeviceID);
  S(AudioObjectGetPropertyData(kAudioObjectSystemObject, &DevicePropertyAddress, 0, nullptr, &AudioDeviceQuerySize, &DeviceAudioObjectID));

  // Obtiene el rango de tamaños de buffer soportado por el dispositivo
  DevicePropertyAddress.mSelector = kAudioDevicePropertyBufferFrameSizeRange;
  DevicePropertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
  DevicePropertyAddress.mElement = kAudioObjectPropertyElementMaster;
  AudioValueRange BufferSizeRange = { 0, 0 };
  AudioDeviceQuerySize = sizeof(AudioValueRange);
  S(AudioObjectGetPropertyData(DeviceAudioObjectID, &DevicePropertyAddress, 0, nullptr, &AudioDeviceQuerySize, &BufferSizeRange));

  // Obtiene el nombre del dispositivo
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

  // Asgina el tamaño de buffer dado por la configuración al ejecutar el programa
  UInt32 frames = BUFFER_SIZE;
  AudioDeviceQuerySize = sizeof(UInt32);
  DevicePropertyAddress.mSelector = kAudioDevicePropertyBufferFrameSize;
  DevicePropertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
  DevicePropertyAddress.mElement = 0;
  AudioObjectSetPropertyData(DeviceAudioObjectID, &DevicePropertyAddress, 0, nullptr, AudioDeviceQuerySize, &frames);

  /* Comprueba
  AudioDeviceQuerySize = sizeof(UInt32);
  DevicePropertyAddress.mSelector = kAudioDevicePropertyBufferFrameSize;
  DevicePropertyAddress.mScope = kAudioObjectPropertyScopeGlobal;
  DevicePropertyAddress.mElement = 0;
  S(AudioObjectGetPropertyData(DeviceAudioObjectID,&DevicePropertyAddress, 0,nullptr, &AudioDeviceQuerySize, &frames));
  //printf("Frames:%u\n",frames);
  */

  // Asigna la frecuencia de muestreo
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

// Genera los AudioComponents para poder instanciar AudioUnits
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

// Función callback del dispositivo, solicitando llenar su buffer
OSStatus callback(void *inRefCon,AudioUnitRenderActionFlags *ioActionFlags,const AudioTimeStamp *inTimeStamp,UInt32 inBusNumber,UInt32 inNumberFrames,AudioBufferList * ioData)
{
    OSStatus err = noErr;
    start = std::chrono::high_resolution_clock::now();
    err = AudioUnitRender(mixerAU,ioActionFlags,inTimeStamp,inBusNumber,inNumberFrames,ioData);
    callbacksCount++;
    end = std::chrono::high_resolution_clock::now();
    diff = end-start;
    delayAvg += diff.count();

    // Almacena valor máximo
    if(diff.count() > delayMax)
      delayMax = diff.count();

    // Almacena valor mínimo
    if(diff.count() < delayMax)
      delayMin = diff.count();

    if(delayAvg/double(callbacksCount) < delayLimit)
    {
      success = false;
    }

    if(callbacksCount == 50)
    {
      delayAvg = delayAvg/double(callbacksCount);
      stop();
    }

    return err;
}

// Crea la AU de salida
void createOutputAU()
{
  AudioComponentInstanceNew(outputComponent, &outputAU);
  AudioUnitInitialize(outputAU);

  // Le asigna la función callback
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

// Crea la AU mezcladora
void createMixerAU()
{
  AudioComponentInstanceNew(mixerComponent, &mixerAU);
  AudioUnitInitialize(mixerAU);

  // Asigna volumen
  AudioUnitSetParameter(
      mixerAU,
      kStereoMixerParam_Volume,
      kAudioUnitScope_Output,
      0,
      volume,
      0
  );
}

void resetAUs()
{
  // Indica en que momento debe comenzar a reproducir
  AudioTimeStamp startTime;
  memset (&startTime, 0, sizeof(startTime));
  startTime.mFlags = kAudioTimeStampSampleTimeValid;
  startTime.mSampleTime = -1;

  AudioUnitReset(outputAU, kAudioUnitScope_Global, 0);
  //AudioUnitReset(mixerAU, kAudioUnitScope_Global, 0);

  for(int i = 0; i < NUM_TRACKS; i++)
  {
    AudioUnitSetProperty(fileAUs[i], kAudioUnitProperty_ScheduleStartTimeStamp,kAudioUnitScope_Global, 0,&startTime, sizeof(startTime));
    //AudioUnitReset(fileAUs[i], kAudioUnitScope_Global, 0);
    //AudioUnitReset(eqAUs[i], kAudioUnitScope_Global, 0);
    //AudioUnitReset(compressorAUs[i], kAudioUnitScope_Global, 0);
    AudioUnitReset(reverbAUs[i], kAudioUnitScope_Global, 0);
  }

}

void addTrack(int index)
{
  AudioFileID fileID;
  AudioStreamBasicDescription fileFormat;

  AudioComponentInstanceNew(fileComponent,&fileAUs[index]);
  AudioUnitInitialize(fileAUs[index]);

  sprintf(fileName,"./%i_%i.wav",SAMPLE_RATE,BIT_DEPTH);

  // URL del archivo de audio
  CFURLRef inputFileURL = CFURLCreateWithFileSystemPath( kCFAllocatorDefault,CFStringCreateWithCString(NULL, fileName, kCFStringEncodingUTF8), kCFURLPOSIXPathStyle, false);

  AudioFileOpenURL(inputFileURL, kAudioFileReadPermission,0,&fileID);

  // Elimina el url
  CFRelease(inputFileURL);

  UInt32 propSize = sizeof(fileFormat);
  AudioFileGetProperty(fileID,kAudioFilePropertyDataFormat, &propSize, &fileFormat);

  // Asigna el archivo
  AudioUnitSetProperty(fileAUs[index],kAudioUnitProperty_ScheduledFileIDs, kAudioUnitScope_Global,0, &fileID, sizeof(fileID));

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
  rgn.mLoopCount = 100;
  rgn.mStartFrame = 0;
  rgn.mFramesToPlay = nPackets * fileFormat.mFramesPerPacket;
  AudioUnitSetProperty(fileAUs[index], kAudioUnitProperty_ScheduledFileRegion,kAudioUnitScope_Global, 0,&rgn,sizeof(rgn));

  // Indica en que momento debe comenzar a reproducir
  AudioTimeStamp startTime;
  memset (&startTime, 0, sizeof(startTime));
  startTime.mFlags = kAudioTimeStampSampleTimeValid;
  startTime.mSampleTime = -1;
  AudioUnitSetProperty(fileAUs[index], kAudioUnitProperty_ScheduleStartTimeStamp,kAudioUnitScope_Global, 0,&startTime, sizeof(startTime));

  // EQ
  AudioComponentInstanceNew(eqComponent,&eqAUs[index]);
  AudioUnitInitialize(eqAUs[index]);

  UInt32 bands = EQ_BANDS;
  AudioUnitSetProperty(
      eqAUs[index],
      kAUNBandEQProperty_NumberOfBands,
      kAudioUnitScope_Global,
      0,
      &bands,
      sizeof(bands)
  );

  int bandTypes[EQ_BANDS] = {
    kAUNBandEQFilterType_2ndOrderButterworthHighPass,
    kAUNBandEQFilterType_LowShelf,
    kAUNBandEQFilterType_Parametric,
    kAUNBandEQFilterType_Parametric,
    kAUNBandEQFilterType_Parametric,
    kAUNBandEQFilterType_Parametric,
    kAUNBandEQFilterType_Parametric,
    kAUNBandEQFilterType_Parametric,
    kAUNBandEQFilterType_Parametric,
    kAUNBandEQFilterType_Parametric,
    kAUNBandEQFilterType_Parametric,
    kAUNBandEQFilterType_Parametric,
    kAUNBandEQFilterType_Parametric,
    kAUNBandEQFilterType_Parametric,
    kAUNBandEQFilterType_HighShelf,
    kAUNBandEQFilterType_2ndOrderButterworthLowPass
  };

  for(int i = 0; i < EQ_BANDS; i++)
  {
    AudioUnitSetParameter(
        eqAUs[index],
        kAUNBandEQParam_BypassBand+i,
        kAudioUnitScope_Global,
        0,
        0,
        0
    );

    AudioUnitSetParameter(
        eqAUs[index],
        kAUNBandEQParam_FilterType+i,
        kAudioUnitScope_Global,
        0,
        bandTypes[i],
        0
    );
  }

  AudioUnitSetParameter(
      eqAUs[index],
      kAUNBandEQParam_Frequency,
      kAudioUnitScope_Global,
      0,
      5000,
      0
  );

  // Compresor
  AudioComponentInstanceNew(compressorComponent,&compressorAUs[index]);
  AudioUnitInitialize(compressorAUs[index]);

  // Reverb
  AudioComponentInstanceNew(reverbComponent,&reverbAUs[index]);
  AudioUnitInitialize(reverbAUs[index]);

  // Filer -> eq
  AudioUnitConnection connection;
  connection.sourceAudioUnit = fileAUs[index];
  connection.sourceOutputNumber = 0;
  connection.destInputNumber = 0;

  AudioUnitSetProperty(
      eqAUs[index],
      kAudioUnitProperty_MakeConnection,
      kAudioUnitScope_Input,
      index,
      &connection,
      sizeof(connection)
  );

  // EQ -> compressor
  connection.sourceAudioUnit = eqAUs[index];
  connection.sourceOutputNumber = 0;
  connection.destInputNumber = 0;

  AudioUnitSetProperty(
      compressorAUs[index],
      kAudioUnitProperty_MakeConnection,
      kAudioUnitScope_Input,
      0,
      &connection,
      sizeof(connection)
  );

  // compressor -> reverb
  connection.sourceAudioUnit = compressorAUs[index];
  connection.sourceOutputNumber = 0;
  connection.destInputNumber = 0;

  AudioUnitSetProperty(
      reverbAUs[index],
      kAudioUnitProperty_MakeConnection,
      kAudioUnitScope_Input,
      0,
      &connection,
      sizeof(connection)
  );

  // reverb -> mixer
  connection.sourceAudioUnit = reverbAUs[index];
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


// ./benchmark buffersize samplerate bitdepth
int main(int argc,char **argv)
{

  if(argc < 4)
  {
    printf("Ejecutar: ./benchmark buffersize samplerate bitdepth\n");
    printf("Ejemplo : ./benchmark 512 44100 16\n");
    return 1;
  }

  // Lee los argumentos del programa
  BUFFER_SIZE = atoi(argv[1]);
  SAMPLE_RATE = atoi(argv[2]);
  BIT_DEPTH = atoi(argv[3]);

  delayLimit = double(BUFFER_SIZE)/double(SAMPLE_RATE);

  printf("CoreAudio Benchmark\n\n");
  printf("Buffer Size: %i frames\n",BUFFER_SIZE);
  printf("Sample Rate: %i Hz\n",SAMPLE_RATE);
  printf("Bit Depth: %i bits\n",BIT_DEPTH);

  printf("Tracks,ProcessingTimeLimitSecs,ProcessingTimeAvgSecs,ProcessingTimeMinSecs,ProcessingTimeMaxSecs,CPUPer,DeltaUserCPUTimeSecs,DeltaKernelCPUTimeSecs,DeltaWallTimeSecs,ResidentMemoryMB,BuffSize,SampleRate,BitDepth,Success\n");

  // Configura el dispositivo de salida
  configureDevice();

  // Genera los componentes para instanciar los AudioUnits
  genComponents();

  // Crea la unidad de salida
  createOutputAU();

  // Crea la unidad mezcladora
  createMixerAU();

  // Mide rendimiento con 1 a 256 pistas
  while(true)
  {
    // Crea una nueva pista
    addTrack(NUM_TRACKS - 1);

    // Reinicia las AU
    resetAUs();

    getrusage(RUSAGE_SELF, &usage_start);
    gettimeofday(&wall_start, NULL);

    // Ejecuta
    play();

    // Espera
    while(playing){}

    getrusage(RUSAGE_SELF, &usage_now);
    gettimeofday(&wall_now, NULL);

    double wallTime = double(wall_now.tv_sec - wall_start.tv_sec) + double(wall_now.tv_usec- wall_start.tv_usec)/1000000.0;
    double delCPUUser = double(usage_now.ru_utime.tv_sec - usage_start.ru_utime.tv_sec) + double(usage_now.ru_utime.tv_usec - usage_start.ru_utime.tv_usec)/1000000.0;
    double delCPUKern = double(usage_now.ru_stime.tv_sec - usage_start.ru_stime.tv_sec) + double(usage_now.ru_stime.tv_usec - usage_start.ru_stime.tv_usec)/1000000.0;

    double cpuTime = double(usage_now.ru_utime.tv_sec + usage_now.ru_stime.tv_sec  - usage_start.ru_utime.tv_sec - usage_start.ru_stime.tv_sec)
                    + double(usage_now.ru_utime.tv_usec + usage_now.ru_stime.tv_usec - usage_start.ru_utime.tv_usec - usage_start.ru_stime.tv_usec)/1000000.0;
    double cpuPer = cpuTime/wallTime;

    double virMemMB = double(usage_now.ru_maxrss)/1000000.0;


    printf("%i,%f,%f,%f,%f,%f,%f,%f,%f,%f,%i,%i,%i,%s\n",NUM_TRACKS,delayLimit,delayAvg,delayMin,delayMax,cpuPer,delCPUUser,delCPUKern,wallTime,virMemMB,BUFFER_SIZE,SAMPLE_RATE,BIT_DEPTH,success ? "false" : "true");

    if(NUM_TRACKS == MAX_TRACKS)
      finish();
    else
      NUM_TRACKS++;
  }

  return 0;
}
