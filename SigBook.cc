#include <longbeach/signals/SigBook.h>

#include <iostream>
#include <boost/format.hpp>
#include <boost/bind.hpp>
#include <longbeach/core/Error.h>
#include <longbeach/core/LuaCodeGen.h>
#include <longbeach/core/LuabindScripting.h>
#include <longbeach/clientcore/BookLevel.h>
#include <longbeach/signals/SignalBuilder.h>

namespace longbeach {
namespace signals {

using std::cout;

SigBook::SigBook(const instrument_t& instr, const std::string &desc, ClockMonitorPtr clockm,
                 IPriceProviderPtr spRefpp, IBookPtr spBook, size_t num_levels, size_t num_sbvars, int vbose, ReturnMode returnMode)
    : SignalSmonImpl( instr, desc, clockm, vbose )
    , m_spRefpp( spRefpp )
    , m_spBook( spBook )
    , m_vars(num_sbvars, 0.0)
    , m_varsOK(false)
    , m_varsDirty(false)
    , m_numLevels(num_levels)
    , m_numSBvars(num_sbvars)
    , m_returnMode(returnMode)
{
    if (m_spBook->addBookListener(this) == false)
        LONGBEACH_THROW_ERROR_SS("SigBook: Book::addListener returned false");
    if ( !m_spRefpp )
        LONGBEACH_THROW_ERROR_SS("SigBook: was passed a NULL refpp" );

    m_spRefpp->addPriceListener( m_spRefppSub, boost::bind(&SigBook::onPriceChanged, this, _1) );
    if (m_vboseLvl >= 2) {
        cout << "Constructed " << m_desc << " with numw:" << m_numSBvars << std::endl;
    }

    for ( size_t i = 0; i < m_numLevels; ++i )
        allocState( boost::str(boost::format("bid%1%") % i) );
    // not using best ask as a var, so skip it
    for ( size_t i = 1; i < m_numSBvars-(m_numLevels-1); ++i )
        allocState( boost::str(boost::format("ask%1%") % i) );
}

SigBook::~SigBook()
{
    m_spBook->removeBookListener(this);
}

void SigBook::resetVars() const
{
    LONGBEACH_ASSERT(m_vars.size() == m_numSBvars);
    std::fill(m_vars.begin(), m_vars.end(), 0.0);
    m_varsOK = false;
    m_varsDirty = false;
}

void SigBook::reset()
{
    resetVars();
    SignalSmonImpl::reset();
}

void SigBook::updateVars() const
{
    resetVars();

    if ( !m_bSourcesOK ) {
        if (m_vboseLvl >= 2) {
            cout << m_desc << "::updateVars: source(s) are not ok, setting vars to 0.0" << std::endl;
        }
        return;
    }
    // see if there are enough levels to calculate signal and that book is ok
    std::vector<BookLevelCPtr> bbls, abls;
    bool bres = getNBookLevels( *m_spBook, BID, m_numLevels, bbls );
    bool ares = getNBookLevels( *m_spBook, ASK, m_numLevels, abls);
    if (m_spBook->isOK() == false || bres == false || ares == false) {
        if (m_vboseLvl) {
            cout << m_desc << ": Not enough levels:" << m_numLevels << " in book to calculate"
                 << " signals. Setting vars to 0.0" << std::endl;
        }
        return;
    }

    std::vector<double> bavgpx;
    std::vector<double> bttlsz;
    std::vector<double> aavgpx;
    std::vector<double> attlsz;

    bavgpx.resize(m_numLevels, 0.0);
    bttlsz.resize(m_numLevels, 0.0);
    aavgpx.resize(m_numLevels, 0.0);
    attlsz.resize(m_numLevels, 0.0);

    // initialize the avgpxs and ttlszs
    bavgpx[0] = bbls[0]->getPrice();
    bttlsz[0] = bbls[0]->getSize();
    aavgpx[0] = abls[0]->getPrice();
    attlsz[0] = abls[0]->getSize();

    // get the refpp to use in case we need to normalize
    bool refppok;
    double refpx = m_spRefpp->getRefPrice( &refppok );
    if ( !refppok ) {
        // something wrong with refpp, use midpoint between bid and ask of this book instead
        refpx = (aavgpx[0] + bavgpx[0]) * 0.5;
    }

    // for best mkt, only use bid side as a var, so we don't run into singularity problems
    // with RefPx
    m_vars[0] = bavgpx[0];
    if (m_vars[0] < refpx * MaxDiffRefMidpx) {
        m_vars[0] = refpx * MaxDiffRefMidpx;
        if (m_vboseLvl) {
            cout << m_desc << ": diff too large: normalizing bid[0] from "
                 << bavgpx[0] << " to " << m_vars[0] << std::endl;
        }
    }
    int offset = m_numLevels - 1;
    for (size_t i=1; i < m_numLevels; i++) {
        bavgpx[i] = (bavgpx[i-1] * bttlsz[i-1] + bbls[i]->getPrice() * bbls[i]->getSize()) / (bttlsz[i-1] + bbls[i]->getSize());
        bttlsz[i] = bttlsz[i-1] + bbls[i]->getSize();
        aavgpx[i] = (aavgpx[i-1] * attlsz[i-1] + abls[i]->getPrice() * abls[i]->getSize()) / (attlsz[i-1] + abls[i]->getSize());
        attlsz[i] = attlsz[i-1] + abls[i]->getSize();
        // stick em in for both bid and ask sides, but normalize em slightly if the values are too extreme
        m_vars[i] = bavgpx[i];
        if (m_vars[i] < m_vars[i-1] * MaxDownChg) {
            m_vars[i] = m_vars[i-1] * MaxDownChg;
            if (m_vboseLvl) {
                cout << m_desc << ": diff too large: normalizing bid[" << i << "] from "
                     << bavgpx[i] << " to " << m_vars[i] << std::endl;
            }
        }
        // ask is more complicated because it didn't have a first value
        int askindex = i + offset;
        m_vars[askindex] = aavgpx[i];
        if (i == 1) {
            // since there is no ask 0 var, use refpx with an extra amount to sub for first value
            if (m_vars[askindex] > refpx * (0.001 + MaxUpChg) * MaxUpChg) {
                m_vars[askindex] = refpx * (0.001 + MaxUpChg) * MaxUpChg;
                if (m_vboseLvl) {
                    cout << m_desc << ": diff too large: normalizing ask[" << i << "] from "
                         << aavgpx[i] << " to " << m_vars[askindex] << std::endl;
                }
            }
        }
        else {
            if (m_vars[askindex] > m_vars[askindex-1] * MaxUpChg) {
                m_vars[askindex] = m_vars[askindex-1] * MaxUpChg;
                if (m_vboseLvl) {
                    cout << m_desc << ": diff too large: normalizing ask[" << i << "] from "
                         << aavgpx[i] << " to " << m_vars[askindex] << std::endl;
                }
            }
        }
    }

    // setting vars_ok to true will allow getSignalState and printVars to do useful stuff
    m_varsOK = true;
#ifdef _SIGDBG
    printVectorState(stdout);
    cout << std::endl;
#endif
}


void SigBook::onPriceChanged( const IPriceProvider& pp )
{
    notifySignalListeners();
}

void SigBook::onBookChanged( const IBook* pBook, const Msg* pMsg,
                             int32_t bidLevelChanged, int32_t askLevelChanged )
{
    m_varsDirty = true;
    notifySignalListeners();
}

void SigBook::onBookFlushed( const IBook* pBook, const Msg* pMsg )
{
    reset();
}

void SigBook::recomputeState() const
{
    if (m_varsDirty)
    {
        m_varsDirty = false;
        updateVars();
    }

    m_isOK = m_bSourcesOK && m_varsOK && m_spRefpp->isPriceOK();

    double refpx = 0.0;
    // possibly we could still calc signals/print values here even if refpp isn't ok...
    if (!m_isOK) {
        // have to reset vars, cause if there was no update then maybe vars weren't cleared yet
        // this will force all the vars printed out to be 0.0
        resetVars();
    }
    else
        refpx = m_spRefpp->getRefPrice();

    for(size_t i=0; i < m_numSBvars; i++)
    {
        if (refpx)
            switch(m_returnMode)
            {
            case DIFF: m_state[i] = (m_vars[i] - refpx); break;
            case ARITH: m_state[i] = (m_vars[i] - refpx) / refpx * 10000; break;
            case LOG: m_state[i] = log( m_vars[i] / refpx ) * 10000; break;
            default: LONGBEACH_THROW_ERROR_SS("SigBook: invalid return mode"); break;
            }
        else
            m_state[i] = 0.0;
    }
}

/************************************************************************************************/
// SigBookSpec
/************************************************************************************************/

SigBookSpec::SigBookSpec()
    : m_numLevels(4)
    , m_numSBvars(7)
    , m_returnMode(DIFF)
{
}

SigBookSpec::SigBookSpec(const SigBookSpec &e)
    : SignalSpec(e)
    , m_book(IBookSpec::clone(e.m_book))
    , m_sources(e.m_sources)
    , m_numLevels(e.m_numLevels)
    , m_numSBvars(e.m_numSBvars)
    , m_returnMode(e.m_returnMode)
{
}

ISignalPtr SigBookSpec::build(SignalBuilder *builder) const
{
    IPriceProviderPtr priceProv = builder->getPxPBuilder()->buildPxProvider(m_refPxP);
    IBookPtr book = builder->getBookBuilder()->buildBook(m_book);

    std::auto_ptr<SigBook> sb(new SigBook(
            m_book->getInstrument(),
            m_description,
            builder->getClockMonitor(),
            priceProv,
            book,
            m_numLevels,
            m_numSBvars,
            builder->getVerboseLevel(),
            m_returnMode));
    sb->registerWithSourceMonitors(builder->getClientContext(), m_sources);
    return ISignalPtr(sb);
}

void SigBookSpec::checkValid() const
{
    SignalSpec::checkValid();
    if(!m_book)
        LONGBEACH_THROW_ERROR_SS("SigBookSpec " << m_description << ": book is null");
    m_book->checkValid();
    util::checkSourcesValid(m_sources);
}

void SigBookSpec::hashCombine(size_t &result) const
{
    SignalSpec::hashCombine(result);
    boost::hash_combine(result, *m_book);
    boost::hash_combine(result, m_sources);
    boost::hash_combine(result, m_numLevels);
    boost::hash_combine(result, m_numSBvars);
    boost::hash_combine(result, m_returnMode);
}

SigBookSpec *SigBookSpec::clone() const
{
    return new SigBookSpec(*this);
}

bool SigBookSpec::compare(const ISignalSpec *other) const
{
    if(!SignalSpec::compare(other)) return false;

    const SigBookSpec *b = dynamic_cast<const SigBookSpec*>(other);
    if(!b) return false;

    if(*this->m_book != *b->m_book) return false;
    if(this->m_sources != b->m_sources) return false;
    if(this->m_numLevels != b->m_numLevels) return false;
    if(this->m_numSBvars != b->m_numSBvars) return false;
    if(this->m_returnMode != b->m_returnMode) return false;
    return true;
}

void SigBookSpec::print(std::ostream &o, const LuaPrintSettings &ps) const
{
    const LuaPrintSettings onei = ps.next(); // indentation one past current

    o << "(function () -- SigBookSpec " << m_description << '\n'
      << onei.indent() << "local book = " << luaMode(*m_book, onei) << '\n'
      << onei.indent() << "local refPxP = " << luaMode(*m_refPxP, onei) << '\n'
      << onei.indent() << "local sb = SigBookSpec()" "\n"
      << onei.indent() << "sb.book = book" "\n"
      << onei.indent() << "sb.refPxP = refPxP" "\n"
      << onei.indent() << "sb.description = " << luaMode(m_description, onei) << '\n'
      << onei.indent() << "sb.sources = " << luaMode(m_sources, onei) << '\n'
      << onei.indent() << "sb.num_levels = " << luaMode(m_numLevels, onei) << '\n'
      << onei.indent() << "sb.num_sbvars = " << luaMode(m_numSBvars, onei) << '\n'
      << onei.indent() << "sb.return_mode = " << luaMode(m_returnMode, onei) << '\n'
      << onei.indent() << "return sb" "\n"
      << onei.indent() << "end)()";
}

void SigBookSpec::getDataRequirements(IDataRequirements *rqs) const
{
    SignalSpec::getDataRequirements(rqs);
    m_book->getDataRequirements(rqs);
}

bool SigBookSpec::registerScripting(lua_State &state)
{
    // each Spec class must be added to registerScripting in Signals_Scripting.cc
    luabind::module( &state )
        [
            luabind::class_<SigBookSpec, SignalSpec, ISignalSpecPtr>("SigBookSpec")
            .def( luabind::constructor<>() )
            .def_readwrite("book",    &SigBookSpec::m_book)
            .def_readwrite("sources", &SigBookSpec::m_sources)
            .def_readwrite("num_levels", &SigBookSpec::m_numLevels)
            .def_readwrite("num_sbvars", &SigBookSpec::m_numSBvars)
            .def_readwrite("return_mode", &SigBookSpec::m_returnMode)
            ];
    return true;
}

} // namespace signals
} // namespace longbeach
