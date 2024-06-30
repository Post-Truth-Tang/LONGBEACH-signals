/* Contents Copyright 2015 Longbeach Capital LLC. All Rights Reserved. */

#ifndef LONGBEACH_SIGNALS_SIGMA_H
#define LONGBEACH_SIGNALS_SIGMA_H

#include <longbeach/signals/Signal.h>
#include <longbeach/signals/SignalSpec.h>
#include <longbeach/signals/SignalSpecMemberList.h> 

#include <longbeach/clientcore/technicals.h>

#include <longbeach/clientcore/CandlesticksFactory.h>
#include <longbeach/clientcore/ICandlestickListener.h>


namespace longbeach {
namespace signals {

template <typename T> int sign(T val) {
    return (T(0) < val) - (val < T(0));
}

class SigMASpec : public SignalSpecT2<SigMASpec>
{
public: 
    LONGBEACH_DECLARE_SCRIPTING();

    static void initMembers();
    SigMASpec();

    virtual instrument_t getInstrument() const { return m_refPxP->getInstrument(); }
    virtual void checkValid() const;
    virtual void getDataRequirements( IDataRequirements *rqs ) const;
    virtual ISignalPtr build( SignalBuilder *builder ) const;

    source_t m_source;
    std::vector<double> windows;
    std::vector<uint32_t> periods;
    ReturnMode m_mode;
    
};

class SigMA 
    : public SignalSmonImpl
    , public ICandlestickListener
{
 public:
    SigMA( const ClientContextPtr& _cc
           , const CandlesticksFactoryPtr& _cf
           , const std::string& _desc
           , const IPriceProviderPtr& _refpxp
           , const source_t m_source
           , const std::vector<double>& _windows
           , const std::vector<uint32_t>& _periods
           , ReturnMode _mode 
           , bool _vbose 
        );
 private:
    void onUpdate( const longbeach::ICandlestickSeries* series,
		   const longbeach::Candlestick& entry );
    void onInputChange( const IPriceProvider& pxp );
    void recomputeState() const;

    CandlesticksFactoryPtr m_spCandlesticksFactory;
    std::vector<ICandlestickSeriesPtr> m_spSeries;
    IPriceProviderPtr m_refpxp;
    std::vector<Subscription> m_subs;

    std::vector<double> windows;
    std::vector<uint32_t> periods;
    std::vector<double> m_ma;
    std::vector<double> diff;
    double px;
    ReturnMode m_mode;
};

} // longbeach
} // signals

#endif // LONGBEACH_SIGNALS_SIGMA_H
