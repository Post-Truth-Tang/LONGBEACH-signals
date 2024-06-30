#ifndef LONGBEACH_SIGNALS_SIGBOOK_H
#define LONGBEACH_SIGNALS_SIGBOOK_H


#include <longbeach/signals/Signal.h>
#include <longbeach/signals/SignalSpec.h>

namespace longbeach {
namespace signals {

class SigBook
    : public SignalSmonImpl
    , protected IBookListener
{
public:
    SigBook(const instrument_t& instr, const std::string &desc, ClockMonitorPtr clockm,
            IPriceProviderPtr spRefpp, IBookPtr spBook, size_t num_levels, size_t num_sbvars, int vbose, ReturnMode returnMode);
    virtual ~SigBook();

//    static const size_t NumLevels = 4;
//    static const size_t NumSBvars = 7;

    virtual void reset();

protected:
    void resetVars() const;
    void updateVars() const;
    virtual void recomputeState() const;

    // IPriceProvider Listener
    void onPriceChanged( const IPriceProvider& pp );

    // IBookListener interface
    /// Invoked when the subscribed Book changes.
    /// The levelChanged entries are negative if there is no change, or a 0-based depth.
    virtual void onBookChanged( const IBook* pBook, const Msg* pMsg,
                                int32_t bidLevelChanged, int32_t askLevelChanged );
    /// Invoked when the subscribed Book is flushed.
    virtual void onBookFlushed( const IBook* pBook, const Msg* pMsg );

protected:
    IPriceProviderPtr m_spRefpp;
    Subscription m_spRefppSub;
    IBookPtr m_spBook;
    mutable std::vector<double> m_vars;
    mutable bool m_varsOK, m_varsDirty;
//#ifdef UBUNTU
//    static const double MaxDownChg = 0.9975;
//    static const double MaxUpChg = 1.0025;
//    static const double MaxDiffRefMidpx = 0.996;
//#else   
    static constexpr const double MaxDownChg = 0.9975;
    static constexpr const double MaxUpChg = 1.0025;
    static constexpr const double MaxDiffRefMidpx = 0.996;
//#endif
    size_t m_numLevels;
    size_t m_numSBvars;
    ReturnMode m_returnMode;
};
LONGBEACH_DECLARE_SHARED_PTR(SigBook);


/// SignalSpec for SigBook
class SigBookSpec : public SignalSpec
{
public:
    LONGBEACH_DECLARE_SCRIPTING();

    SigBookSpec();
    SigBookSpec(const SigBookSpec &e);

    virtual instrument_t getInstrument() const { return m_book->getInstrument(); }
    virtual ISignalPtr build(SignalBuilder *builder) const;
    virtual void checkValid() const;
    virtual void hashCombine(size_t &result) const;
    virtual bool compare(const ISignalSpec *other) const;
    virtual void print(std::ostream &o, const LuaPrintSettings &ps) const;
    virtual void getDataRequirements(IDataRequirements *rqs) const;
    virtual SigBookSpec *clone() const;

    IBookSpecPtr m_book;
    sources_t m_sources;

    size_t m_numLevels;
    size_t m_numSBvars;
    ReturnMode m_returnMode;

};
LONGBEACH_DECLARE_SHARED_PTR(SigBookSpec);



} // namespace signals
} // namespace longbeach

#endif // LONGBEACH_SIGNALS_SIGBOOK_H
