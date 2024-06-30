#include "SigMACD.h"
#include <boost/assign/list_of.hpp>
#include <longbeach/signals/SignalBuilder.h>
#include <longbeach/signals/SignalsPriority.h>
#include <longbeach/clientcore/PriceProviderBuilder.h>

namespace longbeach {
namespace signals {

/************************************************************************************************/
// SigMACDSpec
/************************************************************************************************/

SigMACDSpec::SigMACDSpec()
{
    initMembers();
}

void SigMACDSpec::initMembers()
{
    if( !MemberList::m_bInitialized )
    {
        MemberList::className("SigMACD");
        MemberList::add( "description",   &SigMACDSpec::m_description );
        MemberList::add( "short_window",  &SigMACDSpec::short_window );
        MemberList::add( "long_window",   &SigMACDSpec::long_window );
        MemberList::add( "mid_window",    &SigMACDSpec::mid_window );
        MemberList::add( "refPxP",        &SigMACDSpec::m_refPxP );
        MemberList::m_bInitialized = true;
    }
}

bool SigMACDSpec::registerScripting(lua_State &state)
{
    initMembers();
    LONGBEACH_REGISTER_SCRIPTING_ONCE( state, "longbeach::signals::SigMACDSpec" );
    // each Spec class must be added to registerScripting in Signals_Scripting.cc
    luabind::module( &state )
    [
        luabind::class_<SigMACDSpec, SignalSpec, ISignalSpecPtr>("SigMACDSpec")
            .def( luabind::constructor<>() )
            .def_readwrite("short_window",  &SigMACDSpec::short_window)
            .def_readwrite("long_window",   &SigMACDSpec::long_window)
            .def_readwrite("mid_window",    &SigMACDSpec::mid_window)
            .def_readwrite("refPxP",        &SigMACDSpec::m_refPxP)
    ];
    luaL_dostring( &state, (MemberList::className() + "=SigMACDSpec").c_str() );
    return true;
}

void SigMACDSpec::getDataRequirements(IDataRequirements *rqs) const
{
    SignalSpec::getDataRequirements(rqs);
}

void SigMACDSpec::checkValid() const
{
    SignalSpec::checkValid();
}

ISignalPtr SigMACDSpec::build( SignalBuilder* builder ) const
{
    IPriceProviderPtr ref_pxp = builder->getPxPBuilder()->buildPxProvider(m_refPxP);
    return ISignalPtr(new SigMACD( builder->getClientContext()
                                   , ref_pxp
                                   , short_window
                                   , long_window
                                   , mid_window
                                   , getDescription()
                                   , builder->getVerboseLevel()
                          ));
}

/************************************************************************************************/
// SigMACD
/************************************************************************************************/

SigMACD::SigMACD( const ClientContextPtr& cc
                  , const IPriceProviderPtr& ref
                  , int32_t s
                  , int32_t l
                  , int32_t m
                  , const std::string& desc
                  , bool vbose
    )
    : SignalSmonImpl( ref->getInstrument(), desc, cc->getClockMonitor(), vbose )
    , m_ref_pxp(ref)
    , m_macd( s, l, m )
{
    using namespace boost::assign;
    initSignalStates( list_of("macd")("dea")("diff") );

    m_ref_pxp->addPriceListener( m_sub, boost::bind( &SigMACD::onInputChange, this, _1 ) );
}

void SigMACD::onInputChange( const IPriceProvider& pxp )
{
    if(pxp.isPriceOK())
    {
        double px = pxp.getRefPrice();

        m_macd.update( px );

        setDirty(true);
        setOK(true);
        notifySignalListeners();
    }
}

void SigMACD::recomputeState() const
{
    if(isOK() && m_macd.get_dea())
    {
        setSignalState( 0, m_macd.get_osc().get() );
        setSignalState( 1, m_macd.get_dea().get() );
        setSignalState( 2, m_macd.get_diff().get() );
        setDirty(false);
    }
    else
    {
        setSignalState( 0, 0 );
        setSignalState( 1, 0 );
        setSignalState( 2, 0 );
        setDirty(false);
    }
}

} // namespace signals
} // namespace longbeach

