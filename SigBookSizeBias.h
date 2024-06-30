#ifndef LONGBEACH_SIGNALS_SIGBOOKSIZEBIAS_H
#define LONGBEACH_SIGNALS_SIGBOOKSIZEBIAS_H

#include <longbeach/clientcore/ClockMonitor.h>
#include <longbeach/clientcore/BookPriceProvider.h>
#include <longbeach/signals/Signal.h>
#include <longbeach/signals/SignalSpec.h>

#include <longbeach/signals/SigSnap.h>


namespace longbeach {
namespace signals {


/// keep history of book size imbalances at multiple levels.
class SigBookSizeBias
    : public SignalStateImpl
    , private IBookListener
    , private IClockListener
{
public:
    typedef std::vector<unsigned int> intervals;

    SigBookSizeBias( const instrument_t& instr, const std::string &desc,
             ClockMonitorPtr cm,
             IBookPtr spBook,
             ptime_duration_t _interval,
             const intervals &interval_list, const uint32_t numLevels, const double power, int vbose);

    virtual ~SigBookSizeBias();

    void setInterval(unsigned int i, unsigned int j); // J is in multiples of INTERVAL

protected:
    // IClockListener interface
    virtual void onWakeupCall(const timeval_t& ctv, const timeval_t& swtv, int reason, void* pData );

    // IBookListener interface
    virtual void onBookChanged( const IBook* pBook, const Msg* pMsg,
                                int32_t bidLevelChanged, int32_t askLevelChanged );
    virtual void onBookFlushed( const IBook* pBook, const Msg* pMsg );

    void _reset();
    void check(timeval_t curtime);
    void recomputeState() const;

    ClockMonitorPtr   m_spCM;
    IBookPtr          m_spBook;

    Snapshot<std::vector<double> >      m_snapshot;
    unsigned int                        m_num;  // number of lags
    ptime_duration_t                    m_interval;
    const uint32_t                      m_numLevels;  // number of book levels.
    const double                        m_power;

    timeval_t                           m_last_check;
    static const int R_CHECK = cm::USER_REASON + 1;
};
LONGBEACH_DECLARE_SHARED_PTR(SigBookSizeBias);


/// SignalSpec for SigBookSizeBias
class SigBookSizeBiasSpec : public SignalSpec
{
public:
    LONGBEACH_DECLARE_SCRIPTING();

    SigBookSizeBiasSpec() {}
    SigBookSizeBiasSpec(const SigBookSizeBiasSpec &e);

    virtual instrument_t getInstrument() const { return m_book->getInstrument(); }
    virtual ISignalPtr build(SignalBuilder *builder) const;
    virtual void checkValid() const;
    virtual void hashCombine(size_t &result) const;
    virtual bool compare(const ISignalSpec *other) const;
    virtual void print(std::ostream &o, const LuaPrintSettings &ps) const;
    virtual void getDataRequirements(IDataRequirements *rqs) const;
    virtual SigBookSizeBiasSpec *clone() const;

    ptime_duration_t                    m_interval;
    std::vector<unsigned int>           m_intervals;

    IBookSpecCPtr                       m_book;
    uint32_t                            m_numLevels;
    double                              m_power;

};
LONGBEACH_DECLARE_SHARED_PTR(SigBookSizeBiasSpec);



} // namespace signals
} // namespace longbeach

#endif // LONGBEACH_SIGNALS_SIGBOOKSIZEBIAS_H
