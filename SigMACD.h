#ifndef LONGBEACH_SIGNALS_SIGMACD_H
#define LONGBEACH_SIGNALS_SIGMACD_H

#include <longbeach/signals/Signal.h>
#include <longbeach/signals/SignalSpec.h>
#include <longbeach/signals/SignalSpecMemberList.h>

#include <longbeach/clientcore/technicals.h>

namespace longbeach {
namespace signals {

///
/// Signal computing MACD on give ref px
///

class SigMACDSpec : public SignalSpecT2<SigMACDSpec>
{
public:
    LONGBEACH_DECLARE_SCRIPTING();
    static void initMembers();

    SigMACDSpec();
    virtual instrument_t getInstrument() const { return m_refPxP->getInstrument(); }
    virtual ISignalPtr build(SignalBuilder *builder) const;
    virtual void checkValid() const;
    // virtual bool compare(const ISignalSpec* other) const;
    // virtual void print(std::ostream &o, const LuaPrintSettings &ps) const;
    virtual void getDataRequirements(IDataRequirements *rqs) const;

    int32_t short_window;
    int32_t long_window;
    int32_t mid_window;
};

class SigMACD
    : public SignalSmonImpl
{
public:
    SigMACD( const ClientContextPtr& cc
             , const IPriceProviderPtr& refpxp
             , const int32_t short_w
             , const int32_t long_w
             , const int32_t mid_w
             , const std::string& desc
             , bool vbose
        );

private:
    void onInputChange( const IPriceProvider& pxp );
    void recomputeState() const;

private:
    IPriceProviderPtr m_ref_pxp;
    technicals::macd_t m_macd;

    Subscription m_sub;
};

} // namespace signals
} // namespace longbeach

#endif // LONGBEACH_SIGNALS_SIGMACD_H

