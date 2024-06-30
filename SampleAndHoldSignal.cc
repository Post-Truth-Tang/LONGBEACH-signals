#include <longbeach/signals/SampleAndHoldSignal.h>

#include <longbeach/core/LuaCodeGen.h>
#include <longbeach/core/LuabindScripting.h>
#include <longbeach/signals/SignalBuilder.h>

namespace longbeach {
namespace signals {

SampleAndHoldSignal::SampleAndHoldSignal(ClockMonitor *cm,
        ISignalPtr subSignal,
        const ptime_duration_t &wakeupInterval,
        const ptime_duration_t &wakeupOffset,
        uint32_t priority)
    : PeriodicWakeup(cm, wakeupInterval, wakeupOffset, priority, false)
    , m_subSignal(subSignal)
    , m_lastIsOK(false)
{
    startPeriodicWakeup();
    m_lastState.resize(subSignal->getStateSize(), 0.0);
}

void SampleAndHoldSignal::onPeriodicWakeup(const timeval_t &ctv, const timeval_t &swtv)
{
    bool isOK = m_subSignal->isOK();

    if(isOK)
    {
        const std::vector<double> &newState = m_subSignal->getSignalState();
        if(!m_lastIsOK || newState != m_lastState)
        {
            m_lastIsOK = isOK;
            m_lastState = newState;
            m_lastChangeTime = m_subSignal->getLastChangeTv();
            m_lastWakeupSwtv = swtv;
            notifySignalListeners();
        }
    }
    else
    {
        if(m_lastIsOK)
        {
            m_lastState.clear();
            m_lastState.resize(m_subSignal->getStateSize(), 0.0);
            m_lastChangeTime = m_subSignal->getLastChangeTv();
            m_lastWakeupSwtv = swtv;
            notifySignalListeners();
        }
    }
}

/************************************************************************************************/
// SampleAndHoldSignalSpec
/************************************************************************************************/

SampleAndHoldSignalSpec::SampleAndHoldSignalSpec(const SampleAndHoldSignalSpec &e)
    : m_subSignal(ISignalSpec::clone(e.m_subSignal))
    , m_wakeupInterval(e.m_wakeupInterval)
    , m_wakeupOffset(e.m_wakeupOffset)
    , m_wakeupPriority(e.m_wakeupPriority)
{
}

ISignalPtr SampleAndHoldSignalSpec::build(SignalBuilder *builder) const
{
    ISignalPtr subSignal = builder->buildSignal(m_subSignal);
    return ISignalPtr(new SampleAndHoldSignal(
            builder->getClockMonitor().get(),
            subSignal,
            m_wakeupInterval,
            m_wakeupOffset,
            m_wakeupPriority));
}

void SampleAndHoldSignalSpec::checkValid() const
{
    ISignalSpec::checkValid();
    m_subSignal->checkValid();
    if(m_wakeupInterval.ticks() == 0)
        LONGBEACH_THROW_ERROR_SS("SampleAndHoldSignal " << m_subSignal->getDescription() << ": wakeup interval is 0");
}

SampleAndHoldSignalSpec *SampleAndHoldSignalSpec::clone() const
{
    return new SampleAndHoldSignalSpec(*this);
}

void SampleAndHoldSignalSpec::hashCombine(size_t &result) const
{
    boost::hash_combine(result, *m_subSignal);
    boost::hash_combine(result, m_wakeupInterval);
    boost::hash_combine(result, m_wakeupOffset);
    boost::hash_combine(result, m_wakeupPriority);
}

bool SampleAndHoldSignalSpec::compare(const ISignalSpec *other) const
{
    const SampleAndHoldSignalSpec *b = dynamic_cast<const SampleAndHoldSignalSpec*>(other);
    if(!b) return false;
    if(*this->m_subSignal != *b->m_subSignal) return false;
    if(this->m_wakeupInterval != b->m_wakeupInterval) return false;
    if(this->m_wakeupOffset != b->m_wakeupOffset) return false;
    if(this->m_wakeupPriority != b->m_wakeupPriority) return false;
    return true;
}

void SampleAndHoldSignalSpec::print(std::ostream &o, const LuaPrintSettings &ps) const
{
    const LuaPrintSettings onei = ps.next(); // indentation one past current

    o << "(function () -- SampleAndHoldSignalSpec" << std::endl
      << onei.indent() << "local shs = SampleAndHoldSignalSpec()" << std::endl
      << onei.indent() << "shs.subSignal = " << luaMode(*m_subSignal, onei) << std::endl
      << onei.indent() << "shs.wakeupInterval = " << luaMode(m_wakeupInterval, onei) << std::endl
      << onei.indent() << "shs.wakeupOffset = " << luaMode(m_wakeupOffset, onei) << std::endl
      << onei.indent() << "shs.wakeupPriority = " << luaMode(m_wakeupPriority, onei) << std::endl
      << onei.indent() << "return shs" << std::endl
      << onei.indent() << "end)()";
}

void SampleAndHoldSignalSpec::getDataRequirements(IDataRequirements *rqs) const
{
    m_subSignal->getDataRequirements(rqs);
}

bool SampleAndHoldSignalSpec::registerScripting(lua_State &state)
{
    // each Spec class must be added to registerScripting in Signals_Scripting.cc
    luabind::module( &state )
    [
        luabind::class_<SampleAndHoldSignalSpec, ISignalSpec, ISignalSpecPtr>("SampleAndHoldSignalSpec")
            .def( luabind::constructor<>() )
            .property("subSignal",
                      &SampleAndHoldSignalSpec::__get_signal_spec,
                      &SampleAndHoldSignalSpec::__set_signal_spec)
        // .def_readwrite("subSignal",      &SampleAndHoldSignalSpec::m_subSignal)
            .def_readwrite("wakeupInterval", &SampleAndHoldSignalSpec::m_wakeupInterval)
            .def_readwrite("wakeupOffset",   &SampleAndHoldSignalSpec::m_wakeupOffset)
            .def_readwrite("wakeupPriority", &SampleAndHoldSignalSpec::m_wakeupPriority)
    ];
    return true;
}

} // namespace signals
} // namespace longbeach
