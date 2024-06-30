#include <longbeach/signals/SigBookSizeBias.h>


#include <boost/format.hpp>
#include <boost/bind.hpp>
#include <longbeach/core/LuaCodeGen.h>
#include <longbeach/core/LuabindScripting.h>
#include <longbeach/signals/SignalBuilder.h>

#include <math.h>

namespace longbeach {
namespace signals {


SigBookSizeBias::SigBookSizeBias( const instrument_t& instr, const std::string &desc,
         ClockMonitorPtr cm,
         IBookPtr spBook,
         ptime_duration_t _interval,
         const intervals &interval_list,
         const uint32_t numLevels,
         const double power, int vbose)
    : SignalStateImpl(instr, desc)
    , m_spCM( cm )
    , m_spBook (spBook)
    , m_snapshot( std::vector<double>() )
    , m_num(interval_list.size())
    , m_interval(_interval)
    , m_numLevels (numLevels)
    , m_power ( power)
    , m_last_check()
{
    m_spBook->addBookListener( this );
    m_spCM->scheduleClockNotice( this, cm::clock_notice(cm::ENDOFDAY,0), PRIORITY_SIGNALS_Signal );

    m_snapshot.setSize(m_num);
    for (unsigned int i = 0; i < m_num; ++i){
        setInterval(i, interval_list[i]);
    }

    for ( size_t lag = 0; lag < m_num; ++lag )
    {
        for ( size_t lvl=0; lvl<m_numLevels; ++lvl ){
            allocState( boost::str(boost::format("lag%1%lvl%2%") % interval_list.at(lag) % lvl) );
        }
    }
}

SigBookSizeBias::~SigBookSizeBias()
{
    m_spBook->removeBookListener( this );
}


void SigBookSizeBias::setInterval(unsigned int i, unsigned int j)
{
    m_snapshot.setInterval(i, j);
}


void SigBookSizeBias::_reset()
{
    // reset the state
    m_snapshot.reset();
    m_last_check = 0;
    m_state.assign( getStateSize(), 0 ); // <-- why is this correct?  this state has m_num * m_numSignals entries
    notifySignalListeners(timeval_t());
}

void SigBookSizeBias::check(timeval_t curtime)
{
    std::vector<double> bookimbVec;

    // m_last_check initial case: first msg.
    m_last_check = curtime;

    for (unsigned int i=0; i<m_numLevels; ++i){
        PriceSize bid = m_spBook->getNthSide( i, BID );
        PriceSize ask = m_spBook->getNthSide( i, ASK );
        //std::cout << ", bdsz=" << bid.sz() << ", aksz=" << ask.sz();
        double baseSize = 1.0;
        if (m_power>0){
            bookimbVec.push_back( pow(bid.sz()+baseSize, m_power) - pow(ask.sz()+baseSize, m_power) );
        }
        else{
            bookimbVec.push_back( log(bid.sz()+baseSize) - log(ask.sz()+baseSize) );
        }
    }

    m_snapshot.onBeat(bookimbVec);
}


void SigBookSizeBias::onBookChanged( const IBook* pBook, const Msg* pMsg,
                            int32_t bidLevelChanged, int32_t askLevelChanged )
{
    if ( pBook->getLastChangeTime() == m_last_check ||
         ( (bidLevelChanged != -1 && uint32_t(bidLevelChanged) > m_numLevels ) &&
           (askLevelChanged != -1 && uint32_t(askLevelChanged) > m_numLevels ) ) )
        return;

    check( pBook->getLastChangeTime() );
    notifySignalListeners(pBook->getLastChangeTime());
}


void SigBookSizeBias::onBookFlushed( const IBook* pBook, const Msg* pMsg )
{
    notifySignalListeners(pBook->getLastChangeTime());
}


void SigBookSizeBias::onWakeupCall( const timeval_t& ctv, const timeval_t& swtv, int reason, void* pData)
{
    // at open, reset
    if ( reason == cm::ATOPEN )
    {
        std::cout << m_spCM->getTime() << " resetting " << getDesc() << std::endl;
        _reset();
    }
    // at endofday, schedule ATOPEN and next ENDOFDAY
    else if ( reason == cm::ENDOFDAY )
    {
        std::vector<cm::clock_notice> cns = cm::getClockNotice( m_spCM->getSessionParams(), m_instr, m_spCM->getYMDDate(), cm::ATOPEN );
        if ( cns.size() > 0 )
            m_spCM->scheduleClockNotices( this, cns, PRIORITY_SIGNALS_Signal );
        cns = cm::getClockNotice( m_spCM->getSessionParams(), m_instr, m_spCM->getYMDDate(), cm::ENDOFDAY );
        if ( cns.size() > 0 )
            m_spCM->scheduleClockNotices( this, cns, PRIORITY_SIGNALS_Signal );
    }
}

void SigBookSizeBias::recomputeState() const
{

    m_isOK = m_spBook->isOK();
    if(!m_isOK)
    {
        m_state.assign( m_state.size(), 0.0);
        return;
    }

    unsigned int idx = 0;
    for ( unsigned int lag = 0; lag < m_num; ++lag){
        for ( unsigned int lvl = 0; lvl < m_numLevels; ++lvl){
            if (m_snapshot.getValAt(lag).size()==m_numLevels)
                m_state[idx] = m_snapshot.getValAt(lag).at(lvl);
            else
                m_state[idx] = 0.0;

            ++idx;
        }
    }
}

/************************************************************************************************/
// SigBookSizeBiasSpec
/************************************************************************************************/

SigBookSizeBiasSpec::SigBookSizeBiasSpec(const SigBookSizeBiasSpec &e)
    : SignalSpec(e)
    , m_interval(e.m_interval)
    , m_intervals(e.m_intervals)
    , m_book(IBookSpec::clone(e.m_book))
    , m_numLevels(e.m_numLevels)
    , m_power(e.m_power)
{
}

ISignalPtr SigBookSizeBiasSpec::build(SignalBuilder *builder) const
{
    IBookPtr book = builder->getBookBuilder()->buildBook(m_book);

    return ISignalPtr(new SigBookSizeBias(
            book->getInstrument(),
            m_description,
            builder->getClockMonitor(),
            book,
            m_interval,
            m_intervals,
            m_numLevels,
            m_power,
            builder->getVerboseLevel() ));
}


void SigBookSizeBiasSpec::checkValid() const
{
    SignalSpec::checkValid();
    if(m_interval == ptime_duration_t())
        LONGBEACH_THROW_ERROR_SS("SigBookSizeBiasSpec " << m_description << ": interval is zero");
    if(!m_book)
        LONGBEACH_THROW_ERROR_SS("SigBookSizeBiasSpec " << m_description << ": book is null");
    m_book->checkValid();
    if(m_numLevels == 0)
        LONGBEACH_THROW_ERROR_SS("SigBookSizeBiasSpec " << m_description << ": numLevels is zero");
    if (m_power < 0 )
        LONGBEACH_THROW_ERROR_SS("SigBookSizeBiasSpec " << m_description << ": m_power is negative");
}

SigBookSizeBiasSpec *SigBookSizeBiasSpec::clone() const
{
    return new SigBookSizeBiasSpec(*this);
}

void SigBookSizeBiasSpec::hashCombine(size_t &result) const
{
    SignalSpec::hashCombine(result);
    boost::hash_combine(result, m_interval);
    boost::hash_combine(result, m_intervals);
    boost::hash_combine(result, *m_book);
    boost::hash_combine(result, m_numLevels);
    boost::hash_combine(result, m_power);
}


bool SigBookSizeBiasSpec::compare(const ISignalSpec *other) const
{
    if(!SignalSpec::compare(other)) return false;

    const SigBookSizeBiasSpec *b = dynamic_cast<const SigBookSizeBiasSpec*>(other);

    if(!b) return false;
    if(this->m_interval != b->m_interval) return false;
    if(this->m_intervals != b->m_intervals) return false;
    if(*this->m_book != *b->m_book) return false;
    if(this->m_numLevels != b->m_numLevels) return false;
    if(this->m_power != b->m_power) return false;
    return true;
}

void SigBookSizeBiasSpec::print(std::ostream &o, const LuaPrintSettings &ps) const
{
    const LuaPrintSettings onei = ps.next(); // indentation one past current

    o << "(function () -- SigBookSizeBiasSpec " << m_description << std::endl
      << onei.indent() << "local book = " << luaMode(*m_book, onei) << std::endl
      << onei.indent() << "local sbszbias = SigBookSizeBiasSpec()" << std::endl
      << onei.indent() << "sbszbias.interval = " << luaMode(m_interval, onei) << std::endl
      << onei.indent() << "sbszbias.description = " << luaMode(m_description, onei) << std::endl
      << onei.indent() << "sbszbias.intervals = " << luaMode(m_intervals, onei) << std::endl
      << onei.indent() << "sbszbias.book = book" << std::endl
      << onei.indent() << "sbszbias.numLevels = " << luaMode(m_numLevels, onei) << std::endl
      << onei.indent() << "sbszbias.power = " << luaMode(m_power, onei) << std::endl
      << onei.indent() << "return sbszbias" << std::endl
      << onei.indent() << "end)()";
}

void SigBookSizeBiasSpec::getDataRequirements(IDataRequirements *rqs) const
{
    SignalSpec::getDataRequirements(rqs);
    m_book->getDataRequirements(rqs);
}


bool SigBookSizeBiasSpec::registerScripting(lua_State &state)
{
    // each Spec class must be added to registerScripting in Signals_Scripting.cc
    luabind::module( &state )
    [
        luabind::class_<SigBookSizeBiasSpec, SignalSpec, ISignalSpecPtr>("SigBookSizeBiasSpec")
            .def( luabind::constructor<>() )
            .def_readwrite("interval",  &SigBookSizeBiasSpec::m_interval)
            .def_readwrite("intervals", &SigBookSizeBiasSpec::m_intervals)
            .def_readwrite("book",      &SigBookSizeBiasSpec::m_book)
            .def_readwrite("numLevels", &SigBookSizeBiasSpec::m_numLevels)
            .def_readwrite("power",     &SigBookSizeBiasSpec::m_power)
    ];
    return true;
}


} // namespace signals
} // namespace longbeach
