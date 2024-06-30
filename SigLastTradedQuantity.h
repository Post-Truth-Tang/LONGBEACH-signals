#ifndef LONGBEACH_SIGNALS_SIGLASTTRADEDQUANTITY_H
#define LONGBEACH_SIGNALS_SIGLASTTRADEDQUANTITY_H

#include <longbeach/core/ptime.h>

#include <longbeach/clientcore/PriceProvider.h>
#include <longbeach/clientcore/PriceSizeProviderSpec.h>
#include <longbeach/clientcore/TickProvider.h>

#include <longbeach/signals/Signal.h>
#include <longbeach/signals/SignalSpec.h>


namespace longbeach {
namespace signals {

template <typename T> int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}

class TradedQuantity
{
public:
    TradedQuantity( const TradeTick& trade_tick, const IBookPtr book, const double last_best_bid, const double last_best_ask
                    , const double last_midprice, const boost::optional<double> notional_price );

    const bool isExpired( const timeval_t expiration_time ) const { return (m_tradeTime <= expiration_time);}

    const bool inWindow(const timeval_t current_time, ptime_duration_t window_duration) const
        {
            timeval_t cutoff_time = current_time - window_duration;
            return (m_tradeTime >= cutoff_time && m_tradeTime<=current_time);
        }

    const int32_t getTradedQuantity() const { return m_tradedQuantity;};
    const timeval_t getTradeTime() const { return m_tradeTime;};

private:
    timeval_t m_tradeTime;
    int32_t m_tradedQuantity;
};


class RollingWindow
{
public:

    RollingWindow( longbeach::ptime_duration_t window_length, double smoothing_factor );
    virtual ~RollingWindow(){};

    virtual void update(TradedQuantity traded_quantity);

    virtual double getSignal() const;

    void reset();

    void expireTradedQuantities(const timeval_t current_time);
protected:
    double smooth( double old_value, double new_value, double smoothing_factor);

    longbeach::ptime_duration_t m_length;

    double m_signal;

private:
    std::vector<TradedQuantity> m_tradedQuantities;
    double m_expireSmoothingFactor;
    double m_smoothExpiryAdjustment;
};


class SigLastTradedQuantity
    : public SignalStateImpl
    , private IClockListener
    , private ITickListener
    , private IBookListener
{
public:
    SigLastTradedQuantity( const instrument_t& instr, const std::string &desc, 
                           ClientContextPtr cc,
                           ClockMonitorPtr spCM,
                           const std::vector<longbeach::ptime_duration_t>& vWindowDurations,
                           const double expire_smoothing_factor,
                           IBookPtr spBook,
                           ITickProviderCPtr spTickProvider,
                           ReturnMode returnMode );

    /// Destructor
    virtual ~SigLastTradedQuantity();

    const std::vector<longbeach::ptime_duration_t>& getWindowDurations() const
    {   return m_vWindowDurations; }

private:
    // IClockListener interface
    virtual void onWakeupCall( const timeval_t& ctv, const timeval_t& swtv, int reason, void* pData);

    // IBookListener interface
    /// Invoked when the subscribed Book changes.
    /// The levelChanged entries are negative if there is no change, or a 0-based depth.
    virtual void onBookChanged( const IBook* pBook, const Msg* pMsg,
                                int32_t bidLevelChanged, int32_t askLevelChanged );

    /// Invoked when the subscribed Book is flushed.
    virtual void onBookFlushed( const IBook* pBook, const Msg* pMsg )
        {};

        /// This will be called when the ITickProvider's receives a new TradeTick.
    /// That last trade tick is passed.
    /// Spurious calls are allowed.  tp will never be NULL.
    virtual void onTickReceived( const ITickProvider* tp, const TradeTick& tick );

    // we don't care about this
    virtual void onTickVolumeUpdated( const ITickProvider* tp, uint64_t totalVolume ) {};

    void reset();
    virtual void recomputeState() const;

    virtual void updateState() const ;
protected:
    ClockMonitorPtr                 m_spCM;
    IBookPtr                        m_spBook;
    ITickProviderCPtr               m_spTickProvider;
//    Subscription                    m_spBookSub, m_spTickProviderSub;
    std::vector<ptime_duration_t> m_vWindowDurations;

    boost::optional<double> m_lotSize;

    std::vector<RollingWindow*> m_rollingWindows;

    double m_currentBestBidPrice;
    double m_currentBestAskPrice;
    double m_currentMidPrice;

    double m_lastBestBidPrice;
    double m_lastBestAskPrice;
    double m_lastMidPrice;

    timeval_t m_lastBookUpdateTime;
    
    ReturnMode m_returnMode;
};
LONGBEACH_DECLARE_SHARED_PTR( SigLastTradedQuantity );


/// SignalSpec for SigLastTradedQuantity
class SigLastTradedQuantitySpec : public SignalSpec
{
public:
    LONGBEACH_DECLARE_SCRIPTING();

    SigLastTradedQuantitySpec()
        : m_expireSmoothingFactor( 0.20 )
    {};

    SigLastTradedQuantitySpec(const SigLastTradedQuantitySpec &other);

    virtual instrument_t getInstrument() const { return m_inputBook->getInstrument(); }
    virtual ISignalPtr build(SignalBuilder *builder) const;
    virtual void checkValid() const;
    virtual void hashCombine(size_t &result) const;
    virtual bool compare(const ISignalSpec *other) const;
    virtual void print(std::ostream &o, const LuaPrintSettings &ps) const;
    virtual void getDataRequirements(IDataRequirements *rqs) const;
    virtual SigLastTradedQuantitySpec *clone() const;

    IBookSpecPtr  m_inputBook;
    source_t m_tickSource;
    std::vector<longbeach::ptime_duration_t> m_vWindowDurations;
    double m_expireSmoothingFactor;
    ReturnMode m_returnMode;
};
LONGBEACH_DECLARE_SHARED_PTR( SigLastTradedQuantitySpec );


class WindowAtTime
{
public:
    WindowAtTime( const double window_total, const timeval_t sample_time )
        : m_total(window_total)
        , m_time(sample_time)
        {};

    const double getTotal() const { return m_total;};
    const timeval_t getTime() const { return m_time;};

private:
    double m_total;
    timeval_t m_time;
};

class BaselineRollingWindow
    : public RollingWindow
{
public:

    BaselineRollingWindow( longbeach::ptime_duration_t window_length, double expire_smoothing_factor, longbeach::ptime_duration_t sample_period
                           , uint32_t history_length, double smoothing_factor );

    virtual ~BaselineRollingWindow(){};

    virtual void update(TradedQuantity traded_quantity);
    virtual double getSignal() const;

private:
    uint32_t m_numberOfHistorySamples;
    longbeach::ptime_duration_t m_samplePeriod;
    std::vector<TradedQuantity> m_tradedQuantities;

    double m_smoothingFactor;
    double m_smoothedTotalAtStart;
    std::deque<WindowAtTime> m_sampledHistory;
    std::deque<WindowAtTime> m_recentHistory;
};

class SigBaselineLastTradedQuantity
    : public SigLastTradedQuantity
{
public:
    SigBaselineLastTradedQuantity( const instrument_t& instr, const std::string &desc, 
                                   ClientContextPtr cc,
                                   ClockMonitorPtr spCM,
                                   const std::vector<longbeach::ptime_duration_t>& vWindowDurations,
                                   const uint32_t window_history_length,
                                   const uint32_t windows_to_sample,
                                   const double cutoff,
                                   const double expire_smoothing_factor,
                                   const double smoothing_factor,
                                   IBookPtr spBook,
                                   ITickProviderCPtr spTickProvider);

    /// Destructor
    virtual ~SigBaselineLastTradedQuantity() {};
private:
    virtual void updateState() const;
private:
    double m_cutoff;
};
LONGBEACH_DECLARE_SHARED_PTR( SigBaselineLastTradedQuantity );


/// SignalSpec for SigBaselineLastTradedQuantity
class SigBaselineLastTradedQuantitySpec : public SignalSpec
{
public:
    LONGBEACH_DECLARE_SCRIPTING();

    SigBaselineLastTradedQuantitySpec()
        : m_numberOfHistorySamples( 0 )
        , m_cutoff(0.0)
        , m_windowsToSample( 0 )
        , m_expireSmoothingFactor( 0.20 )
        , m_smoothingFactor( 0.12 )
        {};

    SigBaselineLastTradedQuantitySpec(const SigBaselineLastTradedQuantitySpec &other);

    virtual instrument_t getInstrument() const { return m_inputBook->getInstrument(); }
    virtual ISignalPtr build(SignalBuilder *builder) const;
    virtual void checkValid() const;
    virtual void hashCombine(size_t &result) const;
    virtual bool compare(const ISignalSpec *other) const;
    virtual void print(std::ostream &o, const LuaPrintSettings &ps) const;
    virtual void getDataRequirements(IDataRequirements *rqs) const;
    virtual SigBaselineLastTradedQuantitySpec *clone() const;

    IBookSpecPtr  m_inputBook;
    source_t m_tickSource;
    std::vector<longbeach::ptime_duration_t> m_vWindowDurations;
    uint32_t m_numberOfHistorySamples;
    double m_cutoff;
    uint32_t m_windowsToSample;
    double m_expireSmoothingFactor;
    double m_smoothingFactor;
};
LONGBEACH_DECLARE_SHARED_PTR( SigBaselineLastTradedQuantitySpec );

} // namespace signals
} // namespace longbeach


#endif // LONGBEACH_SIGNALS_SIGLASTTRADEDQUANTITY_H
