#ifndef LONGBEACH_SIGNALS_SAMPLEANDHOLDSIGNAL_H
#define LONGBEACH_SIGNALS_SAMPLEANDHOLDSIGNAL_H


#include <longbeach/core/ptime.h>

#include <longbeach/signals/Signal.h>
#include <longbeach/signals/SignalSpec.h>
#include <longbeach/clientcore/PeriodicWakeup.h>

namespace longbeach {
namespace signals {

/// Samples a given signal at fixed intervals.
class SampleAndHoldSignal
    : public SignalImpl
    , public PeriodicWakeup
{
public:
    SampleAndHoldSignal(ClockMonitor *cm, ISignalPtr subSignal,
            const ptime_duration_t &wakeupInterval, const ptime_duration_t &wakeupOffset, uint32_t priority);

    virtual const instrument_t& getInstrument() const
        { return m_subSignal->getInstrument(); }
    virtual const std::string& getDesc() const
        { return m_subSignal->getDesc(); }

    virtual size_t getStateSize() const
        { return m_subSignal->getStateSize(); }
    virtual const std::vector<std::string>& getStateNames() const
        { return m_subSignal->getStateNames(); }

    virtual bool isOK() const
        { return m_lastIsOK; }
    virtual const std::vector<double>& getSignalState() const
        { return m_lastState; }

    /// returns the last time this SampleAndHoldSignal was sampled.
    virtual longbeach::timeval_t getLastChangeTv() const { return m_lastChangeTime; }
    virtual boost::optional<timeval_t> getLastScheduledChangeTv() const { return m_lastWakeupSwtv; }

protected:
    void onPeriodicWakeup(const timeval_t &ctv, const timeval_t &swtv);

    ISignalPtr m_subSignal;
    bool m_lastIsOK;
    std::vector<double> m_lastState;
    longbeach::timeval_t m_lastChangeTime, m_lastWakeupSwtv;
};
LONGBEACH_DECLARE_SHARED_PTR(SampleAndHoldSignal);

/// SampleAndHoldSignalSpec
class SampleAndHoldSignalSpec : public ISignalSpec
{
public:
    LONGBEACH_DECLARE_SCRIPTING();

    SampleAndHoldSignalSpec() {}
    SampleAndHoldSignalSpec(const SampleAndHoldSignalSpec &e);

    virtual ISignalPtr build(SignalBuilder *builder) const;
    virtual void checkValid() const;
    virtual void hashCombine(size_t &result) const;
    virtual bool compare(const ISignalSpec *other) const;
    virtual void print(std::ostream &o, const LuaPrintSettings &ps) const;
    virtual void getDataRequirements(IDataRequirements *rqs) const;
    virtual SampleAndHoldSignalSpec *clone() const;

    virtual instrument_t getInstrument() const { return m_subSignal->getInstrument(); }
    virtual std::string getDescription() const { return m_subSignal->getDescription(); }

    ISignalSpecCPtr __get_signal_spec() const { return m_subSignal; }
    void            __set_signal_spec(ISignalSpecPtr p) { m_subSignal = p; }

    ISignalSpecCPtr m_subSignal;
    ptime_duration_t m_wakeupInterval, m_wakeupOffset;
    uint32_t m_wakeupPriority;
};
LONGBEACH_DECLARE_SHARED_PTR(SampleAndHoldSignalSpec);


} // namespace longbeach
} // namespace signals

#endif // LONGBEACH_SIGNALS_SAMPLEANDHOLDSIGNAL_H
