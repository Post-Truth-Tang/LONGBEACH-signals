#include <longbeach/signals/SigKalmanFilter.h>

#include <iostream>
#include <boost/format.hpp>
#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include <longbeach/core/Error.h>
#include <longbeach/core/LuaCodeGen.h>
#include <longbeach/core/LuabindScripting.h>
#include <longbeach/core/ptime.h>
#include <longbeach/clientcore/ClientContext.h>
#include <longbeach/signals/SignalBuilder.h>
#include <longbeach/math/Workspace.h>

namespace longbeach {
namespace signals {

using std::cout;

/************************************************************************************************/
// SigKalmanFilterSpec
/************************************************************************************************/

SigKalmanFilterSpec::SigKalmanFilterSpec()
    : R(0.01)
    , Q(0.2)
    , step(1)
    , use_dynamic_deltas(true)
{
    if( !MemberList::m_bInitialized )
    {
        MemberList::className("SigKalmanFilter");
        MemberList::add( "description", &SigKalmanFilterSpec::m_description );
        MemberList::add( "refPxP", &SigKalmanFilterSpec::m_refPxP );
 
        MemberList::add( "input", &SigKalmanFilterSpec::input );
        MemberList::add( "R", &SigKalmanFilterSpec::R );
        MemberList::add( "Q", &SigKalmanFilterSpec::Q );
        MemberList::add( "P0", &SigKalmanFilterSpec::P0 );
        MemberList::add( "step", &SigKalmanFilterSpec::step );
        MemberList::add( "use_dynamic_deltas", &SigKalmanFilterSpec::use_dynamic_deltas );
        
        MemberList::m_bInitialized = true;
    }
}

ISignalPtr SigKalmanFilterSpec::build(SignalBuilder *builder) const
{
    IPriceProviderPtr priceProv = builder->getPxPBuilder()->buildPxProvider(input);
    // IBookPtr book = builder->getBookBuilder()->buildBook(m_book);

    std::auto_ptr<SigKalmanFilter> sb(new SigKalmanFilter(
            builder->getClientContext(),
            m_description,
            *this,
            priceProv,
            builder->getVerboseLevel() ));
    // sb->registerWithSourceMonitors(builder->getClientContext(), m_sources);
    return ISignalPtr(sb);
}

void SigKalmanFilterSpec::checkValid() const
{
    SignalSpec::checkValid();
    input->checkValid();
    // util::checkSourcesValid(m_sources);
}

bool SigKalmanFilterSpec::compare(const ISignalSpec* other) const
{
    return MemberList::compare( this, other );  // we should be able to use this too
}

void SigKalmanFilterSpec::print(std::ostream &o, const LuaPrintSettings &ps) const
{
    MemberList::print( this, luaStream( o, ps ) );
}

void SigKalmanFilterSpec::getDataRequirements(IDataRequirements *rqs) const
{
    SignalSpecT2::getDataRequirements(rqs);
    input->getDataRequirements(rqs);
}

bool SigKalmanFilterSpec::registerScripting(lua_State &state)
{
    LONGBEACH_REGISTER_SCRIPTING_ONCE( state, "SigKalmanFilter" );
    // each Spec class must be added to registerScripting in Signals_Scripting.cc
    luabind::module( &state )
    [
        luabind::class_<SigKalmanFilterSpec, SignalSpec, ISignalSpecPtr>("SigKalmanFilterSpec")
            .def( luabind::constructor<>() )
            .def_readwrite("input",    &SigKalmanFilterSpec::input)
            .def_readwrite("R",        &SigKalmanFilterSpec::R)
            .def_readwrite("Q",        &SigKalmanFilterSpec::Q)
            .def_readwrite("step",     &SigKalmanFilterSpec::step)
            .def_readwrite("P0",       &SigKalmanFilterSpec::P0)
            .def_readwrite("use_dynamic_deltas",       &SigKalmanFilterSpec::use_dynamic_deltas)
    ];
    luaL_dostring( &state, "SigKalmanFilter=SigKalmanFilterSpec" );
    return true;
}


/************************************************************************************************/
// SigKalmanFilter
/************************************************************************************************/

SigKalmanFilter::SigKalmanFilter( ClientContextPtr cc
    , const std::string &desc
    , const SigKalmanFilterSpec& spec_
    , const IPriceProviderPtr& pxp
    , int vbose
    )
    : SignalSmonImpl( spec_.getInstrument(), desc, cc->getClockMonitor(), vbose )
    , m_spec(spec_)
    , m_spInputPxP(pxp)
{
    using namespace boost::assign;
    initSignalStates(list_of("px_est")("v_est")("sig_est")("px_pred")("v_pred")("sig_pred"));

    // H = [1 0 0] if we consider px as the only real observation or [1 1 1]?
    double H_[] = { 1, 0, 0,
                    0, 1, 0,
                    0, 0, 0,
    };
    math::WorkspacePtr ws = math::Workspace::create();
    m_kf.init( 3
        , matrix_t::diagonal( ws, 3, spec().R ) // choose a number randomly
        , matrix_t::diagonal( ws, 3, spec().Q )
        , matrix_t( m_kf.numKalmanStates(), m_kf.numKalmanStates(), H_ )
        );
    if( spec().P0.size() == m_kf.numKalmanStates()*m_kf.numKalmanStates() )
        m_kf.setP0(matrix_t( m_kf.numKalmanStates(), m_kf.numKalmanStates(), spec().P0.data() ));

    if ( !m_spInputPxP )
        LONGBEACH_THROW_ERROR_SS("SigKalmanFilter: was passed a NULL refpp" );

    m_spInputPxP->addPriceListener( m_subPxP, boost::bind(&SigKalmanFilter::onPriceChanged, this, _1) );
    // if (m_vboseLvl >= 2) {
    //     cout << "Constructed " << m_desc << " with numw:" << m_numSBvars << std::endl;
    // }
}

void SigKalmanFilter::onPriceChanged( const IPriceProvider& pp )
{
    timeval_t cur_time = pp.getLastChangeTime();
    double dt = 0.5;
    if( spec().use_dynamic_deltas && m_observations.size() > 0 )
    {
        timeval_t last_time = m_observations[0].getTime();
        dt = timeval_diff( cur_time, last_time ).total_microseconds()/1000000.0;
    }
    const double A_[] = { 1, dt, 0.5 * dt * dt,
                          0,  1,            dt,
                          0,  0,             1,
    };
    matrix_t A = matrix_t( m_kf.numKalmanStates(), m_kf.numKalmanStates(), A_ );

    if( m_observations.size() == 0 )
    {
        observation_t init(cur_time);
        init[0] = pp.getRefPrice();
        m_observations.push_front(init);
        m_observations.push_front(init);
        m_kf.update( init, A );  // update twice to prime the velocity estimation

        m_stepCount = 0;
        return;
    }

    if( m_stepCount % spec().step )
    {
        m_stepCount++;
        return;
    }
    m_stepCount = 1;

    // update
    observation_t obs(cur_time);
    double last_v = m_kf.getLastEstimate()[1];
    obs[0] = pp.getRefPrice();
    obs[1] = (obs[0] - m_observations[1][0]) / (2*dt);
    obs[2] = (obs[1] - last_v) / dt;

    m_observations.push_front(obs);
    state_t x_hat = m_kf.update( obs, A );
    // std::cout << K() << std::endl;

    state_t x_pred = m_kf.predict(A);

    setSignalState( 0, x_hat[0] );
    setSignalState( 1, x_hat[1] );
    setSignalState( 2, x_hat[0] - pp.getRefPrice() );
    setSignalState( 3, x_pred[0] );
    setSignalState( 4, x_pred[1] );
    setSignalState( 5, x_pred[0] - pp.getRefPrice() );
    setDirty(false);
    notifySignalListeners();
}

SigKalmanFilter::~SigKalmanFilter()
{
    if( m_vboseLvl > 1 )
        std::cout << getDesc() << " Final Error Cov:\n" << m_kf.P() << std::endl;
}

void SigKalmanFilter::reset()
{
    // resetVars();
    SignalSmonImpl::reset();
    m_observations.clear();
    m_kf.flush();
    m_stepCount = 0;
}

void SigKalmanFilter::recomputeState() const
{
    setOK( sourcesOk() && m_spInputPxP->isPriceOK() );

    if (!isOK()) {
        // have to reset vars, cause if there was no update then maybe vars weren't cleared yet
        // this will force all the vars printed out to be 0.0
        // reset();
    }
}

} // namespace signals
} // namespace longbeach
