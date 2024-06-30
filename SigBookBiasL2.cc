#include <longbeach/signals/SigBookBiasL2.h>

#include <boost/format.hpp>
#include <boost/bind.hpp>
#include <longbeach/core/Error.h>
#include <longbeach/core/LuaCodeGen.h>
#include <longbeach/core/LuabindScripting.h>
#include <longbeach/core/CommoditiesSpecifications.h>
#include <longbeach/clientcore/BookLevel.h>
#include <longbeach/clientcore/clientcoreutils.h>
#include <longbeach/signals/SignalBuilder.h>

#include <longbeach/clientcore/Message_macros.h>
#include <longbeach/core/messages_autogen.h>

#include <cppformat/format.h>

namespace longbeach {
namespace signals {

SigBookBiasL2::SigBookBiasL2( const instrument_t& instr, const std::string &desc,
                              const ClientContextPtr& spCC, const SigBookBiasL2Spec& spec,
                              IBookPtr spBook )
    : SignalStateImpl(instr, desc)
    , m_spCM( spCC->getClockMonitor() )
    , m_spBook( spBook )
//    , m_volFilter( seconds(spec.m_volFilterWindow) )
    , m_updatePeriod( seconds(10) )
    , m_lambda( spec.m_lambda )
{
    if ( !m_spCM )
        LONGBEACH_THROW_ERROR_SS( "SigBookBiasL2: Bad ClockMonitor" );
    if ( !m_spBook )
        LONGBEACH_THROW_ERROR_SS( "SigBookBiasL2: Bad IBook" );

    // get tick size
//    {
//        boost::optional<CommoditySpecification> entry = spCC->getCommoditiesSpecificationsMap()->findByInstrument(instr);
//        LONGBEACH_THROW_EXASSERT_SSX( bool(entry), "No entry found in instrument specifications list for " << instr );
//        m_ticksize = entry->getTickSize();
////        std::cout << "ticksize:" << m_ticksize << std::endl;
//    }
    // {
    //     m_ticksize = get_tick_size( spCC, instr );
    //     LONGBEACH_THROW_EXASSERT_SSX( bool(m_ticksize), "Could not find tick size for " << instr );
    // }

//    m_spBook->addBookListener( this );
    if( m_spBook->getSource().type() == SRC_CRYPTO )
    {
        spCC->getEventDistributor()->subscribeEvents( m_subMsg
            , boost::bind( &SigBookBiasL2::onMsg, this, _1 )
            , m_spBook->getSource(), CryptoOrderDepthMsg::kMType, m_spBook->getInstrument(), PRIORITY_SIGNALS_Signal );
    }
    if( m_spBook->getSource().type() == SRC_WIND_STOCK )
    {
        spCC->getEventDistributor()->subscribeEvents( m_subMsg
            , boost::bind( &SigBookBiasL2::onMsg, this, _1 )
            , m_spBook->getSource(), WindStockMarketDataMsg::kMType, m_spBook->getInstrument(), PRIORITY_SIGNALS_Signal );
    }
    if( m_spBook->getSource().type() == SRC_MH_L2 )
    {
        spCC->getEventDistributor()->subscribeEvents( m_subMsg
            , boost::bind( &SigBookBiasL2::onMsg, this, _1 )
            , m_spBook->getSource(), MhMdMsg::kMType, m_spBook->getInstrument(), PRIORITY_SIGNALS_Signal );
    }
    if( m_spBook->getSource().type() == SRC_GD_ETF )
    {
        spCC->getEventDistributor()->subscribeEvents( m_subMsg
            , boost::bind( &SigBookBiasL2::onMsg, this, _1 )
            , m_spBook->getSource(), GdEtfQdMsg::kMType, m_spBook->getInstrument(), PRIORITY_SIGNALS_Signal );
    }

    /*
    m_spCM->scheduleWakeupCall( m_subUpdate
            , boost::bind( &SigBookBiasL2::updateVolatility, this, _1, _2 )
            , m_spCM->getTime() + m_updatePeriod
            , PRIORITY_CC_Misc );
    */

    m_spCM->scheduleClockNotice( this, cm::clock_notice(cm::ENDOFDAY,0), PRIORITY_SIGNALS_Signal );

    // setup state names
    allocState( "bias0" );
//    allocState( "vol" );
}


SigBookBiasL2::~SigBookBiasL2()
{
}

namespace
{
//bool checkMhL2Book( const IBookPtr& book, double ticksize )
bool checkMhL2Book( const IBookPtr& book )
{
    return  book->isOK();
    //  && (getBestMarket(*book).getSpread() < 5*m_ticksize) ) //filter out wide market probably due to bad data)
    // && (fabs(getNthMarket(*book,4).getSpread()) < 15*ticksize); //filter out wide market probably due to bad data)
}
}

/*
void SigBookBiasL2::updateVolatility( const timeval_t& ctv, const timeval_t& swtv )
{
    // schedule next call
    m_spCM->scheduleWakeupCall( m_subUpdate
            , boost::bind( &SigBookBiasL2::updateVolatility, this, _1, _2 )
            , m_spCM->getTime() + m_updatePeriod
            , PRIORITY_CC_Misc );

    if ( m_isOK )
    {
        if( m_prevMidPx )
        {
            double midpx = m_spBook->getMidPrice();
            double midpx0 = m_prevMidPx.get();
//            double ret = log( midpx / midpx0 ) * 10000;
            double ret = midpx - midpx0;
            m_volFilter.update( m_spCM->getTime(), ret );
//            std::cout << "updateVolatility mid:" << midpx << " midpx0:" << midpx0
//                    << "ret:" << ret << " volatility:" << m_volFilter.getVolatility() << std::endl;
        }
        m_prevMidPx = m_spBook->getMidPrice();
    }

}
*/

void SigBookBiasL2::onMsg( const Msg& msg )
{
    //m_isOK = checkMhL2Book(m_spBook,m_ticksize.get());
    m_isOK = checkMhL2Book(m_spBook);

    LONGBEACH_MESSAGE_SWITCH_BEGIN( &msg )
    LONGBEACH_MESSAGE_CASE( CryptoOrderDepthMsg, market_data )
//        std::cout << m_spCM->getTime() << " onMsg " << *market_data << std::endl;
        if( market_data->getInstr().sym == m_spBook->getInstrument().sym )
        {
            if( m_isOK )
            {
                notifySignalListeners(m_spCM->getTime());
            }
        }
    LONGBEACH_MESSAGE_CASE_END()
    LONGBEACH_MESSAGE_CASE( WindStockMarketDataMsg, market_data )
//        std::cout << m_spCM->getTime() << " onMsg " << *market_data << std::endl;
        if( market_data->getInstr() == m_spBook->getInstrument() )
        {
            if( m_isOK )
            {
                notifySignalListeners(m_spCM->getTime());
            }
        }
    LONGBEACH_MESSAGE_CASE_END()
    LONGBEACH_MESSAGE_CASE( MhMdMsg, market_data )
//        std::cout << m_spCM->getTime() << " onMsg " << *market_data << std::endl;
        if( market_data->getInstr() == m_spBook->getInstrument() )
        {
            if( m_isOK )
            {
                notifySignalListeners(m_spCM->getTime());
            }
        }
    LONGBEACH_MESSAGE_CASE_END()
    LONGBEACH_MESSAGE_CASE( GdEtfQdMsg, market_data )
//        std::cout << m_spCM->getTime() << " onMsg " << *market_data << std::endl;
        if( market_data->getInstr() == m_spBook->getInstrument() )
        {
            if( m_isOK )
            {
                notifySignalListeners(m_spCM->getTime());
            }
        }
    LONGBEACH_MESSAGE_CASE_END()
    LONGBEACH_MESSAGE_SWITCH_END()


}

void SigBookBiasL2::onBookFlushed( const IBook* pBook, const Msg* pMsg )
{
    notifySignalListeners(pBook->getLastChangeTime());
}


void SigBookBiasL2::onWakeupCall( const timeval_t& ctv, const timeval_t& swtv, int reason, void* pData)
{
    // at open, reset
    if ( reason == cm::ATOPEN )
    {
        _reset();
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


void SigBookBiasL2::_reset()
{
    // reset the state
    m_state.assign( 1, 0 );
    notifySignalListeners(timeval_t());
}
    
std::pair<double,double> SigBookBiasL2::evalSideWeightedAvgPriceSize( side_t side, double midpx ) const
{
//    std::cout << m_spCM->getTime() << std::endl << *m_spBook << std::endl;
    double total_sz = 0;
    double total_pxsz = 0;
    IBookLevelCIterPtr iter = m_spBook->getBookLevelIter(side);
    int32_t levelCnt = 0;
    while( iter->hasNext() )
    {
//        std::cout << "levelCnt:" << levelCnt << std::endl;
        levelCnt = levelCnt + 1;
        BookLevelCPtr level = iter->next();
        double levelPrice = level->getPrice();
        double levelSize = level->getSize();
        if( GT(levelPrice,0) && (levelCnt>1)/*exclude top level*/ )
        {
            double distance = fabs( levelPrice - midpx );
//                double adjust_sz = log( levelSize ) * exp( -m_lambda * distance / vol );
//                double adjust_sz = log( levelSize ) * exp( -m_lambda*distance/m_ticksize.get() );
            double adjust_sz = log( levelSize ) * exp( -m_lambda*distance/midpx*1e3 );
            total_sz += adjust_sz;
            total_pxsz += levelPrice*adjust_sz;
//            fmt::print("side:{} lvl:{} px:{} sz:{} dist:{}\n", side, levelCnt, levelPrice, levelSize, distance);
        }
    }
   // std::cout << " side:" << side
   //    << " total_pxsz:" << total_pxsz
   //    << " total_sz:" << total_sz
   //    << " total_pxsz/total_sz:" << total_pxsz/total_sz
   //    << " vol:" << vol
   //    << std::endl;
    double total_px = (total_sz>0) ? total_pxsz/total_sz : midpx;
    return std::pair<double,double>( total_px, total_sz );
}

void SigBookBiasL2::recomputeState() const
{
    // std::cout << "\n" << *m_spBook << std::endl;
    // double vol = m_volFilter.getVolatility();
    // vol = std::min( std::max( vol, 0.1*m_ticksize.get() ), 5*m_ticksize.get() );
    double midpx = m_spBook->getMidPrice();
    std::pair<double,double> bid = evalSideWeightedAvgPriceSize( BID, midpx );
    std::pair<double,double> ask = evalSideWeightedAvgPriceSize( ASK, midpx );
    // double avgpx = (bid.first*ask.second + ask.first*bid.second) / (bid.second + ask.second);
    double avgpx = ((bid.second>0)&&(ask.second>0)) ?
            ((bid.first*ask.second + ask.first*bid.second) / (bid.second + ask.second))
            : midpx ;
//    std::cout << "bid:" << bid.second << " ask:" << ask.second << " avgpx:" << avgpx << " midpx:" << midpx << std::endl;
    double sig = ( avgpx - midpx ) / midpx * 1e4;
//    sig = std::max( -vol, std::min( vol, sig ) ) / midpx * 1e4;
//    double sig = bid.second - ask.second;
//    std::cout << "refpx:" << refpx << " avgpx:" << avgpx << std::endl;

    setSignalState( 0, sig );
//    std::cout << m_spCM->getTime() << " SigBookBiasL2::recomputeState sig " << sig << std::endl;
//    setSignalState( 1, vol );
    return;
}

/************************************************************************************************/
// SigBookBiasL2Spec
/************************************************************************************************/

SigBookBiasL2Spec::SigBookBiasL2Spec(const SigBookBiasL2Spec &e)
    : SignalSpec(e)
    , m_book(IBookSpec::clone(e.m_book))
//    , m_volFilterWindow(300)
    , m_lambda(e.m_lambda)
{
}

ISignalPtr SigBookBiasL2Spec::build(SignalBuilder *builder) const
{
//    IPriceProviderPtr priceProv = builder->getPxPBuilder()->buildPxProvider(m_refPxP);
    IBookPtr book = builder->getBookBuilder()->buildBook(m_book);

    return ISignalPtr(new SigBookBiasL2(
            book->getInstrument(),
            m_description,
            builder->getClientContext(),
            *this,
//            priceProv,
            book));
}

void SigBookBiasL2Spec::checkValid() const
{
    SignalSpec::checkValid();
    if(!m_book)
        LONGBEACH_THROW_ERROR_SS("SigBookBiasL2Spec " << m_description << ": book is null");
//    if(m_volFilterWindow<0)
//        LONGBEACH_THROW_ERROR_SS("SigBookBiasL2Spec " << m_description << ": vol_filter_window is negative");
    if(m_lambda<0)
        LONGBEACH_THROW_ERROR_SS("SigBookBiasL2Spec " << m_description << ": lambda is negative");
    m_book->checkValid();
}

SigBookBiasL2Spec *SigBookBiasL2Spec::clone() const
{
    return new SigBookBiasL2Spec(*this);
}

void SigBookBiasL2Spec::hashCombine(size_t &result) const
{
    SignalSpec::hashCombine(result);
    boost::hash_combine(result, *m_book);
}

bool SigBookBiasL2Spec::compare(const ISignalSpec *other) const
{
    if(!SignalSpec::compare(other)) return false;

    const SigBookBiasL2Spec *b = dynamic_cast<const SigBookBiasL2Spec*>(other);
    if(!b) return false;

    if(*this->m_book != *b->m_book) return false;
    return true;
}

void SigBookBiasL2Spec::print(std::ostream &o, const LuaPrintSettings &ps) const
{
    const LuaPrintSettings onei = ps.next(); // indentation one past current

    o << "(function () -- SigBookBiasL2Spec " << m_description << std::endl
      << onei.indent() << "local refPxP = " << luaMode(*m_refPxP, onei) << std::endl
      << onei.indent() << "local book = " << luaMode(*m_book, onei) << std::endl
      << onei.indent() << "local sbbias = SigBookBiasL2Spec()" << std::endl
      << onei.indent() << "sbbias.description = " << luaMode(m_description, onei) << std::endl
      << onei.indent() << "sbbias.refPxP = refPxP" << std::endl
      << onei.indent() << "sbbias.book = book" << std::endl
      << onei.indent() << "return sbbias" << std::endl
      << onei.indent() << "end)()";
}

void SigBookBiasL2Spec::getDataRequirements(IDataRequirements *rqs) const
{
    SignalSpec::getDataRequirements(rqs);
    m_book->getDataRequirements(rqs);
}

bool SigBookBiasL2Spec::registerScripting(lua_State &state)
{
    // each Spec class must be added to registerScripting in Signals_Scripting.cc
    luabind::module( &state )
        [
            luabind::class_<SigBookBiasL2Spec, SignalSpec, ISignalSpecPtr>("SigBookBiasL2Spec")
            .def( luabind::constructor<>() )
            .def_readwrite("book",      &SigBookBiasL2Spec::m_book)
//            .def_readwrite("vol_filter_window",    &SigBookBiasL2Spec::m_volFilterWindow)
            .def_readwrite("lambda",    &SigBookBiasL2Spec::m_lambda)
            ];
    return true;
}


} // namespace signals
} // namespace longbeach
