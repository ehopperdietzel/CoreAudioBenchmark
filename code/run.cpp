#include <iostream>
#include <cstdlib>
#include <unistd.h>

int SAMPLE_RATES[3] = {44100,48000,96000};
int BIT_DEPTHS[3] = {16,24,32};
int BUFFER_SIZES[7] = {1024,512,256,128,64};
int result,tracks;
bool overloaded;
char command[129];

int main(int argc,char **argv)
{

  printf("Max Tracks\tBuffer Size\tSample Rate\tBit Depth\n");

  // Loop sample rates
  for(int sr = 0; sr < 3; sr++)
  {
    // Loop bit depths
    for(int bd = 0; bd < 3; bd++)
    {
      // Loop buffer sizes
      for(int bs = 0; bs < 7; bs++)
      {
        // Reinicia el indicador de overload
        overloaded = false;

        // Inicia con 10 pistas
        tracks = 10;

        while(true)
        {
          // Crea comando
          sprintf(command,"./benchmark %i %i %i %i",tracks,BUFFER_SIZES[bs],SAMPLE_RATES[sr],BIT_DEPTHS[bd]);

          // Ejecuta el benchmark
          result = system(command);

          // Si aún no se satura y success
          if(!overloaded && result == 0)
            tracks+=11;
          // Si se acaba de saturar
          else if(!overloaded && result != 0)
          {
            overloaded = true;
            tracks-=1;
          }
          // Si ya se saturó pero aún no se encuentra el máximo
          else if(overloaded && result != 0)
            tracks-=1;
          // Si se encontró el máximo
          else
            break;

          //sleep(2);
        }

        printf("%i\t\t%i\t\t%i\t\t%i\n",tracks,BUFFER_SIZES[bs],SAMPLE_RATES[sr],BIT_DEPTHS[bd]);

      }
      sleep(5);
    }
  }

  printf("\nBenchmark Finalizado.\n");
  return 0;
}
