#include <longbeach/signals/SigLastTradedQuantity.h>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/bind.hpp>
#include <longbeach/core/Error.h>
#include <longbeach/core/LuaCodeGen.h>
#include <longbeach/core/LuabindScripting.h>
#include <longbeach/signals/SignalBuilder.h>
#include <longbeach/clientcore/clientcoreutils.h>
#include <longbeach/clientcore/ShfeTickProvider.h>

namespace longbeach {
namespace signals {

TradedQuantity::TradedQuantity( const TradeTick& trade_tick, const IBookPtr book, const double last_best_bid
                                , const double last_best_ask, const double last_midprice
                                , const boost::optional<double> notional_price)
    : m_tradeTime( trade_tick.getMsgTime() )
    , m_tradedQuantity(0)
{
    bool average_price_confirms_buyers = false;
    bool average_price_confirms_sellers = false;

    if( notional_price )
    {
        average_price_confirms_buyers = notional_price.get() > last_midprice;
        average_price_confirms_sellers = notional_price.get() < last_midprice;
    }

//    bool midprice_moved_up = book->getMidPrice() > last_midprice;
//    bool midprice_moved_down = book->getMidPrice() < last_midprice;
    bool selling = average_price_confirms_sellers; // && midprice_moved_down;
    if( selling )
    {
        m_tradedQuantity = -1 * std::sqrt(trade_tick.getSize());
    }

    bool buying = average_price_confirms_buyers; // && midprice_moved_up;
    if( buying )
    {
        m_tradedQuantity = std::sqrt(trade_tick.getSize());
    }

    bool normalTrading = GT( last_best_ask, 0 ) && GT( last_best_bid, 0 );
    if ( !normalTrading )
    {
        m_tradedQuantity = 0; 
    }

    // std::cout << "SigLastTradedQuantity:" 
    // << " notional_price:" << notional_price.get()
    // << " last_midprice:" << last_midprice
    // << " tradeSize:" << trade_tick.getSize()
    // << " selling:" << selling
    // << " buying:" << buying 
    // << " normalTrading:" << normalTrading 
    // << " m_tradedQuantity:" << m_tradedQuantity 
    // << std::endl;


}

RollingWindow::RollingWindow( longbeach::ptime_duration_t window_length,  double smoothing_factor )
    : m_length( window_length )
    , m_signal( 0.0 )
    , m_expireSmoothingFactor( smoothing_factor )
    , m_smoothExpiryAdjustment( 0.0 )
{
//    if( m_length < ptime_duration_from_double(1.0) )
//        LONGBEACH_THROW_ERROR_SS( " Rolling window cannot have less than second size " );
    m_tradedQuantities.clear();
}

double RollingWindow::smooth( double old_value, double new_value, double smoothing_factor )
{
    return ( new_value * smoothing_factor + (1 - smoothing_factor)*old_value );
}

void RollingWindow::update(TradedQuantity traded_quantity )
{
    if( traded_quantity.inWindow( traded_quantity.getTradeTime(), m_length ) )
    {
        m_tradedQuantities.push_back( traded_quantity );
        m_signal += traded_quantity.getTradedQuantity();
    }

    expireTradedQuantities( traded_quantity.getTradeTime() );

    double unsmoothed_signal = 0.0;
    for(std::vector<TradedQuantity>::const_iterator it = m_tradedQuantities.begin(); it != m_tradedQuantities.end(); ++it)
        unsmoothed_signal += double(it->getTradedQuantity());
    // std::cout << "RollingWindow::update debug: unadjusted_signal:" << unsmoothed_signal << std::endl;

    // std::cout << "RollingWindow::update debug: adjusted_signal:" << m_signal << std::endl;
}

double RollingWindow::getSignal() const
{
//    std::cout << "Rolling::Window debug: signal:" << m_signal << std::endl;
    return m_signal;
}

void RollingWindow::reset()
{
    m_signal = 0.0;
    m_tradedQuantities.clear();
    m_smoothExpiryAdjustment = 0.0;
}

void RollingWindow::expireTradedQuantities(const timeval_t current_time)
{
    timeval_t expiration_time = current_time - m_length;

    // Not assuming that these are time sorted, although they certainly should be.
    // Because of this I find the highest index of an expired quantity and pop
    // off everything up to and including that entry.
    int32_t number_to_erase = -1;
    int32_t total_expired_quantity = 0;
    for( int32_t index = 0; index <= (int32_t(m_tradedQuantities.size()) - 1); index++ )
    {
        TradedQuantity tq = m_tradedQuantities.at(index);
        bool is_expired = tq.isExpired(expiration_time);
        if( is_expired )
        {
//            std::cout << "RollingWindow::Expire debug: tt:" << tq.getTradeTime() << " et:"
//                      << expiration_time << " is_expired:" << is_expired
//                      << std::endl;
            total_expired_quantity += tq.getTradedQuantity();
            number_to_erase = index;
        }
    }
    // Update expiry smoothing adjustment
    if( abs(total_expired_quantity) > 0 )
    {
        m_smoothExpiryAdjustment = smooth( m_smoothExpiryAdjustment, total_expired_quantity, m_expireSmoothingFactor );
//        std::cout << "RollingWindow::Expire debug: expire_adjustment:" << m_smoothExpiryAdjustment
//                  << " total_expired:" << total_expired_quantity
//                  << std::endl;
        m_signal -= m_smoothExpiryAdjustment;
    }

    // Switch from zero-based index to number to pop from the front.
    number_to_erase += 1;

    if( number_to_erase > 0 )
        m_tradedQuantities.erase( m_tradedQuantities.begin(), m_tradedQuantities.begin()+number_to_erase );

}
SigLastTradedQuantity::SigLastTradedQuantity(
        const instrument_t& instr, const std::string &desc, 
        ClientContextPtr cc,
        ClockMonitorPtr spCM,
        const std::vector<longbeach::ptime_duration_t>& vWindowDurations,
        const double expire_smoothing_factor,
        IBookPtr spBook,
        ITickProviderCPtr spTickProvider,
        ReturnMode returnMode
    )
    : SignalStateImpl( instr, desc )
    , m_spCM( spCM )
    , m_spBook( spBook )
    , m_spTickProvider( spTickProvider )
    , m_vWindowDurations( vWindowDurations )
    , m_lotSize( get_contract_size( cc, instr ) )
    , m_currentBestBidPrice(0.0)
    , m_currentBestAskPrice(std::numeric_limits<double>::infinity() )
    , m_currentMidPrice(0.0)
    , m_lastBestBidPrice(0.0)
    , m_lastBestAskPrice(std::numeric_limits<double>::infinity() )
    , m_lastMidPrice(0.0)
    , m_lastBookUpdateTime( longbeach::timeval_t::earliest )
    , m_returnMode( returnMode )
{
    if( !m_lotSize )
        LONGBEACH_THROW_ERROR_SS( "SigTradeBias cannot get contract size for " << instr );
//    m_tradedQuantities.clear();
    m_spCM->scheduleClockNotice( this, cm::clock_notice(cm::ENDOFDAY,0), PRIORITY_SIGNALS_Signal );

    m_spTickProvider->addTickListener( this );
    m_spBook->addBookListener( this );

    m_rollingWindows.clear();
    for ( uint32_t i = 0; i < vWindowDurations.size(); ++i )
    {
        allocState( boost::str(boost::format("wd%1%") % i) );
        m_rollingWindows.push_back( new RollingWindow(vWindowDurations[i], expire_smoothing_factor) );
    }
}

/// Destructor
SigLastTradedQuantity::~SigLastTradedQuantity()
{
    m_spCM->unscheduleClockNotices( this );
    m_spTickProvider->removeTickListener( this );
    m_spBook->removeBookListener( this );
}

void SigLastTradedQuantity::reset()
{
    for( uint32_t i = 0; i < m_vWindowDurations.size(); i++ )
        m_rollingWindows[i]->reset();

    SignalStateImpl::reset(timeval_t());
}


void SigLastTradedQuantity::recomputeState() const
{
    updateState();
}

void SigLastTradedQuantity::updateState() const
{
    m_state.clear();
    for( uint32_t i = 0; i < m_vWindowDurations.size(); i++ )
    {
        double signal = m_rollingWindows[i]->getSignal();
        switch( m_returnMode )
        {
        case ARITH:
            m_state.push_back( signal ); break;
        case LOG:
            double sigtopush ;
            sigtopush = ( signal==0.0 ) ? 0.0 : ( sgn(signal) * log(fabs(signal)) ); 
            m_state.push_back( sigtopush ); break;
        default:
            LONGBEACH_THROW_ERROR_SS( "SigLastTradedQuantity: invalid return mode"); break;
        }
    }
}

void SigLastTradedQuantity::onWakeupCall( const timeval_t& ctv, const timeval_t& swtv, int reason, void* pData)
{
    // at open, reset
    if ( reason == cm::ATOPEN )
    {
        reset();
    }
    // at endofday, schedule ATOPEN and next ENDOFDAY
    else if ( reason == cm::ENDOFDAY )
    {
        std::vector<cm::clock_notice> cns = cm::getClockNotice( m_spCM->getSessionParams(), m_instr, m_spCM->getYMDDate(), cm::ATOPEN );
        m_spCM->scheduleClockNotices( this, cns, PRIORITY_SIGNALS_Signal );
        cns = cm::getClockNotice( m_spCM->getSessionParams(), m_instr, m_spCM->getYMDDate(), cm::ENDOFDAY );
        m_spCM->scheduleClockNotices( this, cns, PRIORITY_SIGNALS_Signal );
    }
}

void SigLastTradedQuantity::onBookChanged( const IBook* pBook, const Msg* pMsg,
                                int32_t bidLevelChanged, int32_t askLevelChanged )
{
    // Book updated first, as expected
    m_lastBestBidPrice = m_currentBestBidPrice;
    m_lastBestAskPrice = m_currentBestAskPrice;
    m_lastMidPrice = m_currentMidPrice;

    m_currentBestBidPrice = pBook->getNthSide(0,longbeach::BID).getPrice();
    m_currentBestAskPrice = pBook->getNthSide(0,longbeach::ASK).getPrice();
    m_currentMidPrice = pBook->getMidPrice();

    m_lastBookUpdateTime = pMsg->hdr->time_sent;
}

void SigLastTradedQuantity::onTickReceived( const ITickProvider* tp, const TradeTick& tick )
{
    m_isOK = m_spBook->isOK() && m_spTickProvider->isLastTickOK();
    if( m_isOK )
    {
        // Remove expired TradedQuantities
        const timeval_t trade_time = tick.getMsgTime();
        // Expect the book to get updated first.
        double last_bid_price = m_lastBestBidPrice;
        double last_ask_price = m_lastBestAskPrice;
        double last_mid_price = m_lastMidPrice;
        if( trade_time > m_lastBookUpdateTime )
        {
            // Tick got updated before the book
            last_bid_price = m_currentBestBidPrice;
            last_ask_price = m_currentBestAskPrice;
            last_mid_price = m_currentMidPrice;
        }

        // Check for initial conditions
        // if( last_ask_price != std::numeric_limits<double>::infinity() && last_ask_price != 0.0 )
        // {
        //     // Add new one.
        //     boost::optional<double> avg_notional_price;

        //     const ShfeTickProvider* shfe_tp = dynamic_cast<const ShfeTickProvider*>(tp);
        //     if( shfe_tp )
        //         avg_notional_price = shfe_tp->getAvgPxInLastTick( m_lotSize.get() );

        //     for( uint32_t i = 0; i < m_vWindowDurations.size(); i++ )
        //     {
        //         m_rollingWindows[i]->update(TradedQuantity(tick, m_spBook, last_bid_price
        //                                                 , last_ask_price, last_mid_price
        //                                                 , avg_notional_price) );
        //     }
        //     updateState();
        //     notifySignalListeners(trade_time);
        // }
        // Add new one.
//        boost::optional<double> avg_notional_price;
//        const ShfeTickProvider* shfe_tp = dynamic_cast<const ShfeTickProvider*>(tp);
//        if( shfe_tp )
//            avg_notional_price = shfe_tp->getAvgPxInLastTick( m_lotSize.get() );
        boost::optional<double> avg_notional_price = tp->getAvgPxInLastTick( m_lotSize.get() );

        for( uint32_t i = 0; i < m_vWindowDurations.size(); i++ )
        {
            m_rollingWindows[i]->update(TradedQuantity(tick, m_spBook, last_bid_price
                                                    , last_ask_price, last_mid_price
                                                    , avg_notional_price) );
        }
        updateState();
        notifySignalListeners(trade_time);
    }
}

BaselineRollingWindow::BaselineRollingWindow( longbeach::ptime_duration_t window_length,
                                              double expire_smoothing_factor, longbeach::ptime_duration_t sample_period
                                              , uint32_t history_length, double smoothing_factor )
    : RollingWindow( window_length, expire_smoothing_factor )
    , m_numberOfHistorySamples( history_length )
    , m_samplePeriod( sample_period )
    , m_smoothingFactor( smoothing_factor )
    , m_smoothedTotalAtStart( 0.0 )
{
//    if( m_length < ptime_duration_from_double(1.0) )
//        LONGBEACH_THROW_ERROR_SS( " Rolling window cannot have less than second size " );
    m_sampledHistory.clear();
    m_recentHistory.clear();
}

void BaselineRollingWindow::update(TradedQuantity traded_quantity )
{
    timeval_t current_time = traded_quantity.getTradeTime();

    RollingWindow::update( traded_quantity );

    double window_total = RollingWindow::getSignal();
    // Keeping data to both sample and get stats on longer term history
    // and to generate deltas versus the beginning of the current window.

    // Update recent history: used to generate deltas versus the beginning of the current window.
    m_recentHistory.push_back( WindowAtTime(window_total, current_time) );

    m_smoothedTotalAtStart = smooth(m_smoothedTotalAtStart, m_recentHistory.front().getTotal(), m_smoothingFactor);

    // Remove older history: only want to keep up to one length worth
    longbeach::ptime_duration_t time_since_first_entry = timeval_diff( current_time, m_recentHistory.front().getTime() );
    while( time_since_first_entry > m_length )
    {
        m_recentHistory.pop_front();
        if( m_recentHistory.size() > 0 )
            time_since_first_entry = timeval_diff( current_time, m_recentHistory.front().getTime() );
        else
            time_since_first_entry = ptime_duration_from_double(0.0);
    }

    // Update longer, sampled history: used to generate longer term stats

    // Sample every complete window length
    bool take_sample = m_sampledHistory.size() == 0;
    if( !take_sample )
    {
        longbeach::ptime_duration_t time_since_last_sample = timeval_diff( current_time, m_sampledHistory.back().getTime() );
        take_sample = time_since_last_sample >= m_samplePeriod;
    }
    if( take_sample )
    {
        m_sampledHistory.push_back( WindowAtTime(window_total, current_time) );

         std::cout << "BaselineRollingWindow::update taking_sample: start_time:"
                   <<  m_sampledHistory.back().getTime()
                   << " sampled_history:" << m_sampledHistory.size()
                   << std::endl;

    }

    // Remove unwanted samples.
    while( m_sampledHistory.size() > m_numberOfHistorySamples )
        m_sampledHistory.pop_front();
}

double BaselineRollingWindow::getSignal() const
{
    double average_magnitude = 0.0;
    BOOST_FOREACH(WindowAtTime sample, m_sampledHistory)
    {
        average_magnitude += fabs(sample.getTotal());
    }

    // First sample can be a 0 entry. Don't want to include that in the average.
    if(m_sampledHistory.size() > 1 )
        average_magnitude = average_magnitude / double(m_sampledHistory.size() - 1);

    double current_total = RollingWindow::getSignal();

    // Proxy for: Detect quick and large change relative to this baseline.
    // But directional signals only occur for moving big and quickly away from zero.
    double signal = 0.0;
    if( m_recentHistory.size() > 1 && average_magnitude > 0.0 )
    {
        double delta = current_total - m_smoothedTotalAtStart;
        signal = delta/average_magnitude;

//        std::cout << "BaselineRollingWindow::update debug: total:" << current_total
//                  << " value_at_start_of_window: " << m_smoothedTotalAtStart
//                  << " historic_average: " << average_magnitude
//                  << std::endl;
    }
    std::cout << "BaslineRollingWindow debug: signal:"
              << signal << std::endl;
    return signal;
}


SigBaselineLastTradedQuantity::SigBaselineLastTradedQuantity(
        const instrument_t& instr, const std::string &desc, 
        ClientContextPtr cc,
        ClockMonitorPtr spCM,
        const std::vector<longbeach::ptime_duration_t>& vWindowDurations,
        const uint32_t window_history_length,
        const uint32_t windows_to_sample,
        const double cutoff,
        const double expire_smoothing_factor,
        const double smoothing_factor,
        IBookPtr spBook,
        ITickProviderCPtr spTickProvider
    )
    : SigLastTradedQuantity( instr, desc, cc, spCM, vWindowDurations, expire_smoothing_factor, spBook, spTickProvider, ARITH )
    , m_cutoff(cutoff)
{
    m_rollingWindows.clear();
    for ( uint32_t i = 0; i < vWindowDurations.size(); i++ )
    {
        ptime_duration_t sample_period = vWindowDurations[i]
            * ( double(windows_to_sample) / double(window_history_length) );
        m_rollingWindows.push_back( new BaselineRollingWindow(vWindowDurations[i], expire_smoothing_factor
                                                          , sample_period, window_history_length
                                                          , smoothing_factor) );
    }

}

void SigBaselineLastTradedQuantity::updateState() const
{
    m_state.clear();
    for( uint32_t i = 0; i < m_vWindowDurations.size(); i++ )
    {
        double signal = m_rollingWindows[i]->getSignal();
        if( fabs(signal) < m_cutoff )
            signal = 0.0;
        m_state.push_back( signal );
    }
}

/************************************************************************************************/
// SigLastTradedQuantitySpec
/************************************************************************************************/

SigLastTradedQuantitySpec::SigLastTradedQuantitySpec( const SigLastTradedQuantitySpec &other )
    : SignalSpec(other)
    , m_inputBook(IBookSpec::clone(other.m_inputBook))
    , m_tickSource(other.m_tickSource)
    , m_vWindowDurations(other.m_vWindowDurations)
    , m_expireSmoothingFactor( other.m_expireSmoothingFactor )
    , m_returnMode( other.m_returnMode )
{}

ISignalPtr SigLastTradedQuantitySpec::build(SignalBuilder *builder) const
{
    ITickProviderCPtr spTP = builder->getSourceTickFactory()->getTickProvider( m_inputBook->getInstrument(), m_tickSource, true );
    if ( !spTP )
        LONGBEACH_THROW_ERROR_SS( "handleSigDecayTick cannot create SourceTick for " << m_tickSource );

    IBookPtr book = builder->getBookBuilder()->buildBook(m_inputBook);

    return ISignalPtr(new SigLastTradedQuantity(
            m_inputBook->getInstrument(), m_description,
            builder->getClientContext(),
            builder->getClockMonitor(),
            m_vWindowDurations, m_expireSmoothingFactor, book, spTP, m_returnMode));
}

void SigLastTradedQuantitySpec::checkValid() const
{
    SignalSpec::checkValid();
    if(!m_inputBook)
        LONGBEACH_THROW_ERROR_SS("SigLastTradedQuantity " << m_description << ": inputBook is NULL");
    if( !m_tickSource.isValid() )
        LONGBEACH_THROW_ERROR_SS("SigLastTradedQuantity " << m_description << ": tickSource is NULL");
    if(m_vWindowDurations.size() == 0)
        LONGBEACH_THROW_ERROR_SS("SigLastTradedQuantity " << m_description << ": WindowDurations.size() is 0");
    if(m_expireSmoothingFactor > 1.0 || m_expireSmoothingFactor < 0.0 )
        LONGBEACH_THROW_ERROR_SS("SigLastTradedQuantity " << m_description << ": expiry smoothing factor is not <= 1.0");
}

SigLastTradedQuantitySpec *SigLastTradedQuantitySpec::clone() const
{
    return new SigLastTradedQuantitySpec(*this);
}

void SigLastTradedQuantitySpec::hashCombine(size_t &result) const
{
    SignalSpec::hashCombine(result);
    boost::hash_combine(result, *m_inputBook);
    boost::hash_combine(result, m_tickSource);
    boost::hash_combine(result, m_vWindowDurations);
    boost::hash_combine(result, m_expireSmoothingFactor);
    boost::hash_combine(result, m_returnMode);
}

bool SigLastTradedQuantitySpec::compare(const ISignalSpec *other) const
{
    if(!SignalSpec::compare(other)) return false;

    const SigLastTradedQuantitySpec *b = dynamic_cast<const SigLastTradedQuantitySpec*>(other);
    if(!b) return false;
    if(*this->m_inputBook != *b->m_inputBook) return false;
    if(this->m_tickSource != b->m_tickSource) return false;
    if(this->m_vWindowDurations != b->m_vWindowDurations) return false;
    if(this->m_expireSmoothingFactor != b->m_expireSmoothingFactor) return false;
    if(this->m_returnMode != b->m_returnMode) return false;
    return true;
}

void SigLastTradedQuantitySpec::print(std::ostream &o, const LuaPrintSettings &ps) const
{
    const LuaPrintSettings onei = ps.next(); // indentation one past current

    o << "(function () -- SigLastTradedQuantitySpec " << m_description << std::endl
      << onei.indent() << "local inputBook = " << luaMode(*m_inputBook, onei) << std::endl
      << onei.indent() << "local sltq = SigLastTradedQuantitySpec()" << std::endl
      << onei.indent() << "sltq.description = " << luaMode(m_description, onei) << std::endl
      << onei.indent() << "sltq.book = inputBook" << std::endl
      << onei.indent() << "sltq.tick_source = " << luaMode(m_tickSource, onei) << std::endl
      << onei.indent() << "sltq.window_durations = DurationVec" << luaMode(m_vWindowDurations, onei) << std::endl
      << onei.indent() << "sltq.expire_smoothing_factor = " << luaMode(m_expireSmoothingFactor, onei) << std::endl
      << onei.indent() << "sltq.return_mode = " << luaMode( m_returnMode, onei ) << std::endl
      << onei.indent() << "return sltq" << std::endl
      << onei.indent() << "end)()";
}

void SigLastTradedQuantitySpec::getDataRequirements(IDataRequirements *rqs) const
{
    SignalSpec::getDataRequirements(rqs);
    m_inputBook->getDataRequirements(rqs);
//    m_inputTickProvider->getDataRequirements(rqs);
}

bool SigLastTradedQuantitySpec::registerScripting(lua_State &state)
{
    // each Spec class must be added to registerScripting in Signals_Scripting.cc
    luabind::module( &state )
    [
        luabind::class_<SigLastTradedQuantitySpec, SignalSpec, ISignalSpecPtr>("SigLastTradedQuantitySpec")
            .def( luabind::constructor<>() )
            .def_readwrite( "window_durations",        &SigLastTradedQuantitySpec::m_vWindowDurations )
            .def_readwrite( "expire_smoothing_factor", &SigLastTradedQuantitySpec::m_expireSmoothingFactor )
            .def_readwrite( "book",                    &SigLastTradedQuantitySpec::m_inputBook )
            .def_readwrite( "tick_source",             &SigLastTradedQuantitySpec::m_tickSource )
            .def_readwrite( "return_mode",             &SigLastTradedQuantitySpec::m_returnMode )
    ];
    return true;
}

/************************************************************************************************/
// SigBaselineLastTradedQuantitySpec
/************************************************************************************************/

SigBaselineLastTradedQuantitySpec::SigBaselineLastTradedQuantitySpec( const SigBaselineLastTradedQuantitySpec &other )
    : SignalSpec(other)
    , m_inputBook(IBookSpec::clone(other.m_inputBook))
    , m_tickSource(other.m_tickSource)
    , m_vWindowDurations(other.m_vWindowDurations)
    , m_numberOfHistorySamples( other.m_numberOfHistorySamples )
    , m_cutoff( other.m_cutoff )
    , m_windowsToSample( other.m_windowsToSample )
    , m_expireSmoothingFactor( other.m_expireSmoothingFactor )
    , m_smoothingFactor( other.m_smoothingFactor )
{}

ISignalPtr SigBaselineLastTradedQuantitySpec::build(SignalBuilder *builder) const
{
    ITickProviderCPtr spTP = builder->getSourceTickFactory()->getTickProvider( m_inputBook->getInstrument(), m_tickSource, true );
    if ( !spTP )
        LONGBEACH_THROW_ERROR_SS( "handleSigDecayTick cannot create SourceTick for " << m_tickSource );

    IBookPtr book = builder->getBookBuilder()->buildBook(m_inputBook);

    return ISignalPtr(new SigBaselineLastTradedQuantity(
            m_inputBook->getInstrument(), m_description,
            builder->getClientContext(),
            builder->getClockMonitor(),
            m_vWindowDurations, m_numberOfHistorySamples, m_windowsToSample, m_cutoff
            , m_expireSmoothingFactor, m_smoothingFactor, book, spTP));
}

void SigBaselineLastTradedQuantitySpec::checkValid() const
{
    SignalSpec::checkValid();
    if(!m_inputBook)
        LONGBEACH_THROW_ERROR_SS("SigBaselineLastTradedQuantity " << m_description << ": inputBook is NULL");
    if( !m_tickSource.isValid() )
        LONGBEACH_THROW_ERROR_SS("SigBaselineLastTradedQuantity " << m_description << ": tickSource is NULL");
    if(m_vWindowDurations.size() == 0)
        LONGBEACH_THROW_ERROR_SS("SigBaselineLastTradedQuantity " << m_description << ": WindowDurations.size() is 0");
    if(m_numberOfHistorySamples == 0)
        LONGBEACH_THROW_ERROR_SS("SigBaselineLastTradedQuantity " << m_description << ": number of history samples is 0");
    if(m_windowsToSample == 0)
        LONGBEACH_THROW_ERROR_SS("SigBaselineLastTradedQuantity " << m_description << ": number of windows to samples is 0");
    if(m_smoothingFactor > 1.0 || m_expireSmoothingFactor < 0.0 )
        LONGBEACH_THROW_ERROR_SS("SigBaselineLastTradedQuantity " << m_description << ": expire smoothing factor is not <= 1.0");
    if(m_smoothingFactor > 1.0 || m_smoothingFactor < 0.0 )
        LONGBEACH_THROW_ERROR_SS("SigBaselineLastTradedQuantity " << m_description << ": smoothing factor is not <= 1.0");
}

SigBaselineLastTradedQuantitySpec *SigBaselineLastTradedQuantitySpec::clone() const
{
    return new SigBaselineLastTradedQuantitySpec(*this);
}

void SigBaselineLastTradedQuantitySpec::hashCombine(size_t &result) const
{
    SignalSpec::hashCombine(result);
    boost::hash_combine(result, *m_inputBook);
    boost::hash_combine(result, m_tickSource);
    boost::hash_combine(result, m_vWindowDurations);
    boost::hash_combine(result, m_numberOfHistorySamples);
    boost::hash_combine(result, m_windowsToSample);
    boost::hash_combine(result, m_cutoff);
    boost::hash_combine(result, m_expireSmoothingFactor);
    boost::hash_combine(result, m_smoothingFactor);
}

bool SigBaselineLastTradedQuantitySpec::compare(const ISignalSpec *other) const
{
    if(!SignalSpec::compare(other)) return false;

    const SigBaselineLastTradedQuantitySpec *b = dynamic_cast<const SigBaselineLastTradedQuantitySpec*>(other);
    if(!b) return false;
    if(*this->m_inputBook != *b->m_inputBook) return false;
    if(this->m_tickSource != b->m_tickSource) return false;
    if(this->m_vWindowDurations != b->m_vWindowDurations) return false;
    if(this->m_numberOfHistorySamples != b->m_numberOfHistorySamples) return false;
    if(this->m_windowsToSample != b->m_windowsToSample) return false;
    if(this->m_cutoff != b->m_cutoff) return false;
    if(this->m_expireSmoothingFactor != b->m_expireSmoothingFactor) return false;
    if(this->m_smoothingFactor != b->m_smoothingFactor) return false;
    return true;
}

void SigBaselineLastTradedQuantitySpec::print(std::ostream &o, const LuaPrintSettings &ps) const
{
    const LuaPrintSettings onei = ps.next(); // indentation one past current

    o << "(function () -- SigBaselineLastTradedQuantitySpec " << m_description << std::endl
      << onei.indent() << "local inputBook = " << luaMode(*m_inputBook, onei) << std::endl
      << onei.indent() << "local sbltq = SigBaselineLastTradedQuantitySpec()" << std::endl
      << onei.indent() << "sbltq.description = " << luaMode(m_description, onei) << std::endl
      << onei.indent() << "sbltq.book = inputBook" << std::endl
      << onei.indent() << "sbltq.tick_source = " << luaMode(m_tickSource, onei) << std::endl
      << onei.indent() << "sbltq.window_durations = DurationVec" << luaMode(m_vWindowDurations, onei) << std::endl
      << onei.indent() << "sbltq.total_history_samples = " << luaMode(m_numberOfHistorySamples, onei) << std::endl
      << onei.indent() << "sbltq.windows_to_sample = " << luaMode(m_windowsToSample, onei) << std::endl
      << onei.indent() << "sbltq.cutoff = " << luaMode(m_cutoff, onei) << std::endl
      << onei.indent() << "sbltq.expire_smoothing_factor = " << luaMode(m_expireSmoothingFactor, onei) << std::endl
      << onei.indent() << "sbltq.smoothing_factor = " << luaMode(m_smoothingFactor, onei) << std::endl
      << onei.indent() << "return sbltq" << std::endl
      << onei.indent() << "end)()";
}

void SigBaselineLastTradedQuantitySpec::getDataRequirements(IDataRequirements *rqs) const
{
    SignalSpec::getDataRequirements(rqs);
    m_inputBook->getDataRequirements(rqs);
//    m_inputTickProvider->getDataRequirements(rqs);
}

bool SigBaselineLastTradedQuantitySpec::registerScripting(lua_State &state)
{
    // each Spec class must be added to registerScripting in Signals_Scripting.cc
    luabind::module( &state )
    [
        luabind::class_<SigBaselineLastTradedQuantitySpec, SignalSpec, ISignalSpecPtr>("SigBaselineLastTradedQuantitySpec")
            .def( luabind::constructor<>() )
            .def_readwrite( "window_durations",      &SigBaselineLastTradedQuantitySpec::m_vWindowDurations )
            .def_readwrite( "total_history_samples", &SigBaselineLastTradedQuantitySpec::m_numberOfHistorySamples )
            .def_readwrite( "windows_to_sample",     &SigBaselineLastTradedQuantitySpec::m_windowsToSample )
            .def_readwrite( "book",                  &SigBaselineLastTradedQuantitySpec::m_inputBook )
            .def_readwrite( "tick_source",           &SigBaselineLastTradedQuantitySpec::m_tickSource )
            .def_readwrite( "cutoff",                &SigBaselineLastTradedQuantitySpec::m_cutoff )
            .def_readwrite( "expire_smoothing_factor",      &SigBaselineLastTradedQuantitySpec::m_expireSmoothingFactor )
            .def_readwrite( "smoothing_factor",      &SigBaselineLastTradedQuantitySpec::m_smoothingFactor )
    ];
    return true;
}

} // namespace signals
} // namespace longbeach
