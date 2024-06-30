#include "SigDiff.h"
#include <boost/assign/list_of.hpp>
#include <longbeach/clientcore/EventDist.h>
#include <longbeach/signals/SignalBuilder.h>
#include <longbeach/signals/SignalsPriority.h>
#include <longbeach/clientcore/PriceProviderBuilder.h>
#include <longbeach/math/dataset.h>
#include <longbeach/math/dataset_stats.h>

namespace longbeach {
namespace signals {

/************************************************************************************************/
// SigDiffSpec
/************************************************************************************************/

SigDiffSpec::SigDiffSpec()
{
    initMembers();
}

void SigDiffSpec::initMembers()
{
    if( !MemberList::m_bInitialized )
    {
        MemberList::className("SigDiff");
        MemberList::add( "description", &SigDiffSpec::m_description );
        MemberList::add( "a", &SigDiffSpec::a );
        MemberList::add( "b", &SigDiffSpec::m_refPxP );
        MemberList::m_bInitialized = true;
    }
}

bool SigDiffSpec::registerScripting(lua_State &state)
{
    initMembers();
    LONGBEACH_REGISTER_SCRIPTING_ONCE( state, "longbeach::signals::SigDiffSpec" );
    // each Spec class must be added to registerScripting in Signals_Scripting.cc
    luabind::module( &state )
    [
        luabind::class_<SigDiffSpec, SignalSpec, ISignalSpecPtr>("SigDiffSpec")
            .def( luabind::constructor<>() )
            .def_readwrite("a",    &SigDiffSpec::a)
            .def_readwrite("b",    &SigDiffSpec::m_refPxP)
    ];
    luaL_dostring( &state, (MemberList::className() + "=SigDiffSpec").c_str() );
    return true;
}

void SigDiffSpec::getDataRequirements(IDataRequirements *rqs) const
{
    SignalSpec::getDataRequirements(rqs);
    a->getDataRequirements(rqs);
}

void SigDiffSpec::checkValid() const
{
    SignalSpec::checkValid();
    a->checkValid();
}

ISignalPtr SigDiffSpec::build( SignalBuilder* builder ) const
{
    IPriceProviderPtr a_obj = builder->getPxPBuilder()->buildPxProvider(a);
    IPriceProviderPtr b_obj = builder->getPxPBuilder()->buildPxProvider(m_refPxP);
    return ISignalPtr(new SigDiff( builder->getClientContext()
            , a_obj
            , b_obj
            , getDescription()
            , builder->getVerboseLevel()
            ));
}

/************************************************************************************************/
// SigDiff
/************************************************************************************************/

SigDiff::SigDiff( const ClientContextPtr& cc
    , const IPriceProviderPtr& a
    , const IPriceProviderPtr& b
    , const std::string& desc
    , bool vbose
    )
    : SignalSmonImpl( a->getInstrument(), desc, cc->getClockMonitor(), vbose )
    , m_spED(cc->getEventDistributor())
    , m_evalPriority(PRIORITY_SIGNALS_Signal)
    , m_a(a)
    , m_b(b)
    , m_bEvalScheduled(false)
    , m_tw(seconds(5))
{
    using namespace boost::assign;
    initSignalStates( list_of("d0")("avg") );

    Subscription sub;
    m_a->addPriceListener( sub, boost::bind( &SigDiff::onInputChange, this, _1 ) );
    m_subs.push_back(sub);
    m_b->addPriceListener( sub, boost::bind( &SigDiff::onInputChange, this, _1 ) );
    m_subs.push_back(sub);
}

void SigDiff::onInputChange( const IPriceProvider& pxp )
{
    if(pxp.isPriceOK())
    {
        if(!m_bEvalScheduled)
        {
            LONGBEACH_ASSERT( m_spED->getEventContext().workerPriority() > m_evalPriority );
            m_bEvalScheduled = m_spED->addWork( boost::bind( &SigDiff::eval, this )
                , m_evalPriority );
        }
    }
}

void SigDiff::eval()
{
    bool ok_a = m_a->isPriceOK();
    bool ok_b = m_b->isPriceOK();
    if( ok_a && ok_b )
    {
        double px_a = m_a->getRefPrice();
        double px_b = m_b->getRefPrice();
        double value = px_a - px_b;
        TimeWindow<double>::Entry e( getClockMonitor()->getTime(), value );
        m_tw.push_end(e);
        m_tw.flush_start();

        setDirty(true);
        setOK(true);
        notifySignalListeners();
    }
    else // not ok
    {
        if(isOK())
        {
            setOK(false);
            notifySignalListeners();
        }
    }
    
    m_bEvalScheduled = false;
}

void SigDiff::recomputeState() const
{
    if(isOK())
    {
        // we only get here if a && b
        bool ok_a = false;
        bool ok_b = false;
        double px_a = m_a->getRefPrice(&ok_a);
        double px_b = m_b->getRefPrice(&ok_b);
        if( ok_a && ok_b )
        {
            double value = px_a - px_b;
            datasetT<double> points;
            BOOST_FOREACH( const TimeWindow<double>::Entry& e, m_tw.data() )
            {
                points->push_back(e.data());
            }
            double avg = mean(points);

            setSignalState( 0, value );
            setSignalState( 1, avg );
            setDirty(false);
        }
    }
    else
    {
        setSignalState( 0, 0 );
        setDirty(false);
    }
}

} // namespace signals
} // namespace longbeach

