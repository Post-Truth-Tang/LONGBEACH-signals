#ifndef LONGBEACH_SIGNALS_SIGBOOKBIASL2_H
#define LONGBEACH_SIGNALS_SIGBOOKBIASL2_H


#include <longbeach/clientcore/IBook.h>
#include <longbeach/clientcore/BookPriceProvider.h>
#include <longbeach/signals/Signal.h>
#include <longbeach/signals/SignalSpec.h>

#include <longbeach/math/VolatilityFilter.h>

namespace longbeach {
namespace signals {

/// SignalSpec for SigBookBiasL2
class SigBookBiasL2Spec : public SignalSpec
{
public:
    LONGBEACH_DECLARE_SCRIPTING();

    SigBookBiasL2Spec()
    {}
    SigBookBiasL2Spec(const SigBookBiasL2Spec &e);

    virtual instrument_t getInstrument() const { return m_book->getInstrument(); }
    virtual ISignalPtr build(SignalBuilder *builder) const;
    virtual void checkValid() const;
    virtual void hashCombine(size_t &result) const;
    virtual bool compare(const ISignalSpec *other) const;
    virtual void print(std::ostream &o, const LuaPrintSettings &ps) const;
    virtual void getDataRequirements(IDataRequirements *rqs) const;
    virtual SigBookBiasL2Spec *clone() const;

    IBookSpecPtr    m_book;
//    int32_t         m_volFilterWindow;
    double          m_lambda;
};
LONGBEACH_DECLARE_SHARED_PTR(SigBookBiasL2Spec);


class SigBookBiasL2
    : public SignalStateImpl
    , private IBookListener
    , private IClockListener
{
public:
    /// Constructor
    SigBookBiasL2( const instrument_t& instr, const std::string &desc,
                   const ClientContextPtr& spCC, const SigBookBiasL2Spec& spec,
                   IBookPtr spBook );

    /// Destructor
    virtual ~SigBookBiasL2();

    /// Returns the timeval this signal last changed.
    virtual timeval_t getLastChangeTv() const
    {   return m_lastChangeTv; }

private:
//    void updateVolatility( const timeval_t& ctv, const timeval_t& swtv );
    void onMsg( const Msg& msg );
    std::pair<double,double> evalSideWeightedAvgPriceSize( side_t side, double midpx ) const;

    virtual void onBookFlushed( const IBook* pBook, const Msg* pMsg );

    virtual void onWakeupCall( const timeval_t& ctv, const timeval_t& swtv, int reason, void* pData);

    void _reset();
    virtual void recomputeState() const;

private:
    ClockMonitorPtr       m_spCM;
    IBookPtr              m_spBook;

    //boost::optional<double> m_ticksize;
    boost::optional<double> m_prevMidPx;
    //math::VolatilityFilter m_volFilter;
    duration_t            m_updatePeriod;
    double                m_lambda;

    Subscription          m_subMsg;
    Subscription          m_subUpdate;
};
LONGBEACH_DECLARE_SHARED_PTR( SigBookBiasL2 );


} // namespace signals
} // namespace longbeach

#endif // LONGBEACH_SIGNALS_SigBookBiasL2_H
