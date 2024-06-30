#ifndef LONGBEACH_SIGNALS_SIGKALMANFILTER_H
#define LONGBEACH_SIGNALS_SIGKALMANFILTER_H

#include <longbeach/signals/Signal.h>
#include <longbeach/signals/SignalSpec.h>
#include <longbeach/math/KalmanFilter.h>
#include <longbeach/signals/SignalSpecMemberList.h>

namespace longbeach {
namespace signals {

/// SignalSpec for SigKalmanFilter
class SigKalmanFilterSpec : public SignalSpecT2<SigKalmanFilterSpec>
{
public:
    LONGBEACH_DECLARE_SCRIPTING();

    SigKalmanFilterSpec();

    virtual instrument_t getInstrument() const { return input->getInstrument(); }
    virtual ISignalPtr build(SignalBuilder *builder) const;
    virtual void checkValid() const;
    virtual bool compare(const ISignalSpec* other) const;
    virtual void print(std::ostream &o, const LuaPrintSettings &ps) const;
    virtual void getDataRequirements(IDataRequirements *rqs) const;

    IPriceProviderSpecPtr input;
    double R;
    double Q;
    int32_t step;
    std::vector<double> P0;
    bool use_dynamic_deltas;
};
LONGBEACH_DECLARE_SHARED_PTR(SigKalmanFilterSpec);

class SigKalmanFilter
    : public SignalSmonImpl
    , protected IBookListener
{
public:
    SigKalmanFilter( ClientContextPtr cc, const std::string &desc
        , const SigKalmanFilterSpec& spec
        , const IPriceProviderPtr& pxp
        , int vbose
        );
    virtual ~SigKalmanFilter();

    virtual void reset();

protected:
    const SigKalmanFilterSpec& spec() const { return m_spec; }
    virtual void recomputeState() const;

    // IPriceProvider Listener
    void onPriceChanged( const IPriceProvider& pp );

protected:
    const SigKalmanFilterSpec m_spec;
    IPriceProviderPtr m_spInputPxP;
    Subscription m_subPxP;

    int32_t m_stepCount;

    typedef KalmanFilter<3>::State state_t;
    typedef KalmanFilter<3>::Observation observation_t;
    std::deque<observation_t> m_observations; // may not need this later
    KalmanFilter<3> m_kf;
};
LONGBEACH_DECLARE_SHARED_PTR(SigKalmanFilter);

} // namespace signals
} // namespace longbeach

#endif // LONGBEACH_SIGNALS_SIGKALMANFILTER_H
