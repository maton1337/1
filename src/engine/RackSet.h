#pragma once

#include "Rack.h"

namespace bridgerack
{

/**
    The collection of racks the engine runs, in order, on every output block.
    Rack outputs that target the same device channel are summed.
*/
class RackSet
{
public:
    void prepare (double sampleRate, int maximumBlockSize);
    void release();

    //==============================================================================
    // Message thread
    Rack& addRack (std::unique_ptr<Rack>);
    std::unique_ptr<Rack> removeRack (int index);
    void moveRack (int from, int to);
    std::vector<std::unique_ptr<Rack>> removeAll();

    int size() const noexcept                    { return (int) racks.size(); }
    Rack* getRack (int index) const;
    int indexOf (const Rack*) const;

    /** When on, racks with less latency are delayed so every rack output lines up. */
    void setLatencyCompensation (bool);
    bool isLatencyCompensationOn() const noexcept { return compensate; }

    /** Re-reads plugin latencies; call periodically from the message thread. Returns the aligned latency. */
    int updateLatencyCompensation();

    double getSampleRate() const noexcept        { return sampleRate; }
    int getBlockSize() const noexcept            { return blockSize; }

    //==============================================================================
    /** Audio thread. Clears the outputs, then runs and sums every rack. */
    void process (const float* const* inputs, int numInputs,
                  float* const* outputs, int numOutputs,
                  int numSamples) noexcept;

private:
    juce::SpinLock listLock;   // held by the message thread only for pointer swaps
    std::vector<std::unique_ptr<Rack>> racks;

    double sampleRate = 48000.0;
    int blockSize = 512;
    bool prepared = false;
    bool compensate = false;

    std::vector<const float*> inputChunk;
    std::vector<float*> outputChunk;
};

} // namespace bridgerack
