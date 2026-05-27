#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <string>
#include <atomic>

// Make sure winmm.lib is linked!
#pragma comment(lib, "winmm.lib")

class SoundInstance {
private:
    std::string alias;
    std::string path;
    static std::atomic<int> nextId;
public:
    SoundInstance(const std::string& filePath) {
        path = filePath;
        int id = nextId++;
        alias = "snd_" + std::to_string(id);
        
        // Open the file with mciSendString
        // For filenames with spaces, we wrap them in quotes
        std::string cmd = "open \"" + path + "\" alias " + alias;
        MCIERROR err = mciSendStringA(cmd.c_str(), NULL, 0, NULL);
        if (err != 0) {
            // Fallback for mpegvideo type specifically if extension matches MP3/WAV/etc.
            cmd = "open \"" + path + "\" type mpegvideo alias " + alias;
            mciSendStringA(cmd.c_str(), NULL, 0, NULL);
        }
    }
    
    ~SoundInstance() {
        close();
    }
    
    void play(bool loop = false) {
        // Seek to start
        std::string seekCmd = "seek " + alias + " to start";
        mciSendStringA(seekCmd.c_str(), NULL, 0, NULL);
        
        std::string playCmd = "play " + alias;
        if (loop) {
            playCmd += " repeat";
        }
        mciSendStringA(playCmd.c_str(), NULL, 0, NULL);
    }
    
    void pause() {
        std::string cmd = "pause " + alias;
        mciSendStringA(cmd.c_str(), NULL, 0, NULL);
    }
    
    void resume() {
        std::string cmd = "resume " + alias;
        mciSendStringA(cmd.c_str(), NULL, 0, NULL);
    }
    
    void stop() {
        std::string cmd = "stop " + alias;
        mciSendStringA(cmd.c_str(), NULL, 0, NULL);
    }
    
    void close() {
        std::string cmd = "close " + alias;
        mciSendStringA(cmd.c_str(), NULL, 0, NULL);
    }
    
    void setVolume(double volume) {
        // volume from 0 to 100
        int volVal = static_cast<int>(volume * 10.0);
        if (volVal < 0) volVal = 0;
        if (volVal > 1000) volVal = 1000;
        std::string cmd = "setaudio " + alias + " volume to " + std::to_string(volVal);
        mciSendStringA(cmd.c_str(), NULL, 0, NULL);
    }
};

// Initialize the static atomic integer
inline std::atomic<int> SoundInstance::nextId{0};
