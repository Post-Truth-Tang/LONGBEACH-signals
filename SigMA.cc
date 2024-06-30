#include "SigMA.h"
#include <boost/assign/list_of.hpp>
#include <boost/lexical_cast.hpp>
#include <longbeach/signals/SignalBuilder.h>
#include <longbeach/signals/SignalsPriority.h>
#include <longbeach/signals/Signals_Scripting.h>
#include <longbeach/clientcore/PriceProviderBuilder.h>
#include <longbeach/clientcore/technicals.h>

namespace longbeach{
    namespace signals{
	/********************************************/
	// Spec
	/********************************************/

	SigMASpec::SigMASpec()
	{
	    initMembers();
	}

	void SigMASpec::initMembers()
	{
	    if( !MemberList::m_bInitialized )
		{
		    MemberList::className("SigMA");
		    MemberList::add( "description", &SigMASpec::m_description );
		    MemberList::add( "m_source", &SigMASpec::m_source );
		    MemberList::add( "refPxP", &SigMASpec::m_refPxP );
		    MemberList::add( "windows", &SigMASpec::windows );
		    MemberList::add( "periods", &SigMASpec::periods );
            MemberList::add( "m_mode", &SigMASpec::m_mode );
		    MemberList::m_bInitialized = true;
		}
	}

	bool SigMASpec::registerScripting(lua_State &state)
	{
	    initMembers();
	    LONGBEACH_REGISTER_SCRIPTING_ONCE( state, "longbeach::signals::SigMASpec" );
	    luabind::module( &state )
		[
		 luabind::class_<SigMASpec, SignalSpec, ISignalSpecPtr>("SigMASpec")
		 .def( luabind::constructor<>() )
		 .def_readwrite("source", &SigMASpec::m_source)
		 .def_readwrite("windows", &SigMASpec::windows)
		 .def_readwrite("periods", &SigMASpec::periods)
		 .def_readwrite("refPxP", &SigMASpec::m_refPxP)
         .def_readwrite("return_mode", &SigMASpec::m_mode)
		 ];
	    luaL_dostring( &state, (MemberList::className() + "=SigMASpec").c_str() );
	    return true;
	}

	void SigMASpec::getDataRequirements( IDataRequirements *rqs ) const
	{
	    SignalSpec::getDataRequirements( rqs );
	}

	void SigMASpec::checkValid() const
	{
	    SignalSpec::checkValid();
	}

	ISignalPtr SigMASpec::build( SignalBuilder* builder ) const
	{
	    IPriceProviderPtr ref_pxp = builder->getPxPBuilder()->buildPxProvider(m_refPxP);
	    return ISignalPtr(new SigMA( builder->getClientContext()
                                     , builder->getCandlesticksFactory()
                                     , getDescription()
                                     , ref_pxp
                                     , m_source
                                     , windows
                                     , periods
                                     , m_mode
                                     , builder->getVerboseLevel()
                              ));
	} 

	/********************************************/
	// Signals
	/********************************************/

	SigMA::SigMA( const ClientContextPtr& _cc
                  , const CandlesticksFactoryPtr& _cf
                  , const std::string& _desc
                  , const IPriceProviderPtr& _refpxp
                  , const source_t m_source
                  , const std::vector<double>& _windows
                  , const std::vector<uint32_t>& _periods
                  , ReturnMode _mode
                  , bool _vbose 
		      )
	    : SignalSmonImpl( _refpxp->getInstrument(), _desc, _cc->getClockMonitor(), _vbose )
	    , m_spCandlesticksFactory( _cf )
	    , m_refpxp( _refpxp )
	    , windows( _windows )
	    , periods( _periods )
	    , m_ma(_windows.size(),0)
	    , diff(_windows.size(),0)
	    , px(0.0)
        , m_mode( _mode )
	{
	    using namespace boost::assign;
	    std::vector<std::string> names;
	    for( std::vector<int>::size_type i = 0; i != periods.size(); i ++ )
		{
		    names.push_back( "p" + boost::lexical_cast<std::string>(periods[i]) + "_w" + boost::lexical_cast<std::string>(windows[i])  );
		}
	    initSignalStates(names);

	    Subscription sub;
	    m_refpxp -> addPriceListener( sub, boost::bind( &SigMA::onInputChange, this, _1 ) );
	    m_subs.push_back(sub);

	    for( std::vector<int>::size_type i = 0; i != periods.size(); i ++ )
		{
		    Subscription sub;
		    CandlePtr cand;
		    cand  =  m_spCandlesticksFactory->create2( m_refpxp->getInstrument(), m_source, windows[i] );
		    cand -> subscribe( &sub, this );
		    m_spSeries.push_back( cand -> series() );
		    m_subs.push_back(sub);
		} 
	}

	void SigMA::onUpdate( const longbeach::ICandlestickSeries* series
			      , const longbeach::Candlestick& entry )
	{
	    for( std::vector<int>::size_type i = 0; i != periods.size(); i ++ )
		{
		    m_ma[i] = technicals::ma( technicals::close( m_spSeries[i] ), periods[i] );
		}
	    notifySignalListeners();
	}

	void SigMA::onInputChange( const IPriceProvider& pxp )
	{
	    if( pxp.isPriceOK() )
		{
		    px = pxp.getRefPrice();
		    setDirty( true );
		    setOK( true );
		    notifySignalListeners();
		}
	}

	void SigMA::recomputeState() const
	{
      
	    if( isOK() )
		{
		    for (  std::vector<int>::size_type i = 0; i != periods.size(); i ++ )
			{
                double tmp = longbeach::EQZ( m_ma[i] ) ? 0.0 : px - m_ma[i];
                double sigtopush = 0.0;
                switch( m_mode )
                    {
                    case DIFF: setSignalState( i, tmp ); break;
                    case ARITH: setSignalState( i, tmp ); break;
                    case LOG: 
                        sigtopush = ( tmp == 0.0 ) ? 0.0 : ( sign(tmp) * log(fabs(tmp)) );
                        setSignalState( i, sigtopush ); 
                        break;    
                    default: LONGBEACH_THROW_ERROR_SS("SigMA: invalid return mode"); break;
                    }
			}
		    setDirty( false );
		}
	}
    }
}
