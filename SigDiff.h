#ifndef LONGBEACH_SIGNALS_SIGDIFF_H
#define LONGBEACH_SIGNALS_SIGDIFF_H

#include <longbeach/signals/Signal.h>
#include <longbeach/signals/SignalSpec.h>
#include <longbeach/signals/SignalSpecMemberList.h>

#include <longbeach/core/TimeWindow.h>

namespace longbeach {
namespace signals {

///
/// Signal that is the difference of two price providers
/// i.e. s = a - b
/// Note:m_refPxP is aliased to 'b'
///

class SigDiffSpec : public SignalSpecT2<SigDiffSpec>
{
public:
    LONGBEACH_DECLARE_SCRIPTING();
    static void initMembers();

    SigDiffSpec();
    virtual instrument_t getInstrument() const { return a->getInstrument(); }
    virtual ISignalPtr build(SignalBuilder *builder) const;
    virtual void checkValid() const;
    // virtual bool compare(const ISignalSpec* other) const;
    // virtual void print(std::ostream &o, const LuaPrintSettings &ps) const;
    virtual void getDataRequirements(IDataRequirements *rqs) const;

    IPriceProviderSpecPtr a;
};

class SigDiff
    : public SignalSmonImpl
{
public:
    SigDiff( const ClientContextPtr& cc
        , const IPriceProviderPtr& a
        , const IPriceProviderPtr& b
        , const std::string& desc
        , bool vbose
        );

private:
    void onInputChange( const IPriceProvider& pxp );
    void eval();
    void recomputeState() const;

private:
    EventDistributorPtr m_spED;
    Priority m_evalPriority;
    IPriceProviderPtr m_a;
    IPriceProviderPtr m_b;
    std::vector<Subscription> m_subs;
    bool m_bEvalScheduled;
    TimeWindow<double> m_tw;
};

} // namespace signals
} // namespace longbeach

#endif // LONGBEACH_SIGNALS_SIGDIFF_H

