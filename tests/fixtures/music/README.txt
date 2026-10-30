Samples for tests/music_check.c: the MP3 and Ogg Vorbis paths of the background music. Each is 0.3 s of a 440 Hz
sine (880 Hz on the right channel where the name says stereo), at 14000 of 32768, made from a WAV of the same tone:

  lame -m s -b 64 stereo44100.wav tones-stereo-44100.mp3      (also at 48000 Hz: tones-stereo-48000.mp3)
  lame -m m -b 32 mono22050.wav tone-mono-22050.mp3           (MPEG-2, 22.05 kHz)
  oggenc -q 2 -o tones-stereo-44100.ogg stereo44100.wav
  oggenc -q 2 -o tone-mono-16000.ogg mono16000.wav
  ffmpeg -i mono16000.wav -c:a libopus -b:a 16k tone-opus.ogg (Opus in an Ogg file: not Vorbis, and refused)

lame 3.100, libvorbis 1.3.7 and ffmpeg's libopus produced them; they are test data, never shipped.
