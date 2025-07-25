/*
 * SPDX-FileCopyrightText: <text>Copyright 2024 Arm Limited and/or its
 * affiliates <open-source-office@arm.com></text>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file is part of PAF, the Physical Attack Framework.
 */

#pragma once

#include "PAF/WAN/Signal.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace PAF::WAN {

class Waveform {

    /// This adaptor template class is used to provide an iterator which unwraps
    /// the unique_ptr pointer used in the Waveform's containers.
    template <typename SignalTy> struct SignalsIterator {
      public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = SignalTy;
        using difference_type = SignalTy;
        using pointer = SignalTy *;
        using reference = SignalTy &;

        SignalsIterator(const std::vector<std::unique_ptr<Signal>> *signals,
                        size_t idx)
            : signals(signals), idx(idx) {}
        SignalsIterator() = delete;

        SignalsIterator(const SignalsIterator &) = default;
        SignalsIterator &operator=(const SignalsIterator &) = default;

        /// Iterator can be compared for equality.
        bool operator==(const SignalsIterator &rhs) const {
            return signals == rhs.signals && idx == rhs.idx;
        }
        /// Iterator can be compared for inequality.
        bool operator!=(const SignalsIterator &rhs) const {
            return signals != rhs.signals || idx != rhs.idx;
        }

        /// Iterator can be dereferenced as an rvalue.
        SignalTy &operator*() const {
            assert(idx < signals->size() &&
                   "SignalsIterator not in a dereferenceable state");
            return *(*signals)[idx].get();
        }
        /// Iterator can be dereferenced as an rvalue.
        SignalTy *operator->() const {
            assert(idx < signals->size() &&
                   "SignalsIterator not in a dereferenceable state");
            return (*signals)[idx].get();
        }

        /// Iterator can be pre-incremented.
        SignalsIterator &operator++() {
            ++idx;
            return *this;
        }
        /// Iterator can be post-incremented.
        SignalsIterator operator++(int) {
            SignalsIterator tmp(*this);
            operator++();
            return tmp;
        }
        /// Iterator can be pre-decremented.
        SignalsIterator &operator--() {
            --idx;
            return *this;
        }
        /// Iterator can be post-decremented.
        SignalsIterator operator--(int) {
            SignalsIterator tmp(*this);
            operator--();
            return tmp;
        }

        /// Can be compared with inequality relational operators (<, >, <= and
        /// >=).
        bool operator<(const SignalsIterator &rhs) const {
            assert(signals == rhs.signals && "Uncomparable SignalsIterators");
            return idx < rhs.idx;
        }
        bool operator>(const SignalsIterator &rhs) const {
            assert(signals == rhs.signals && "Uncomparable SignalsIteratos");
            return idx > rhs.idx;
        }
        bool operator<=(const SignalsIterator &rhs) const {
            assert(signals == rhs.signals && "Uncomparable SignalsIterators");
            return idx <= rhs.idx;
        }
        bool operator>=(const SignalsIterator &rhs) const {
            assert(signals == rhs.signals && "Uncomparable SignalsIterators");
            return idx >= rhs.idx;
        }

        /// Iterator supports compound assignment operations +=
        SignalsIterator &operator+=(int n) {
            idx += n;
            return *this;
        }
        /// Iterator supports compound assignment operation -=
        SignalsIterator &operator-=(int n) {
            idx -= n;
            return *this;
        }

        /// Supports substracting an iterator from another.
        int operator-(const SignalsIterator &rhs) const {
            assert(signals == rhs.signals &&
                   "Un-substractable SignalsIterators");
            return idx - rhs.idx;
        }

        /// Supports the offset dereference operator ([])
        SignalTy &operator[](int n) const {
            assert(idx + n < signals->size() &&
                   "SignalsIterator not in a dereferenceable state");
            return *(*signals)[idx + n].get();
        }

      private:
        const std::vector<std::unique_ptr<Signal>> *signals;
        size_t idx;
    };

  public:
    /// The SignalDesc class describes a signal.
    class SignalDesc {
      public:
        enum class Kind : uint8_t { REGISTER, WIRE, INTEGER };

        SignalDesc(std::string_view name, Kind kind, bool alias,
                   SignalIdxTy idx)
            : name(name), idx(idx), kind(kind), alias(alias) {}

        SignalDesc(std::string &&name, Kind kind, bool alias, SignalIdxTy idx)
            : name(std::move(name)), idx(idx), kind(kind), alias(alias) {}

        SignalDesc() = delete;
        SignalDesc(const SignalDesc &) = default;
        SignalDesc(SignalDesc &&) = default;

        SignalDesc &operator=(const SignalDesc &) = default;
        SignalDesc &operator=(SignalDesc &&) = default;

        static SignalDesc Register(std::string_view name, bool alias,
                                   SignalIdxTy idx) {
            return {name, Kind::REGISTER, alias, idx};
        }
        static SignalDesc Wire(std::string_view name, bool alias,
                               SignalIdxTy idx) {
            return {name, Kind::WIRE, alias, idx};
        }
        static SignalDesc Integer(std::string_view name, bool alias,
                                  SignalIdxTy idx) {
            return {name, Kind::INTEGER, alias, idx};
        }

        [[nodiscard]] const std::string &getName() const { return name; }
        [[nodiscard]] Kind getKind() const { return kind; }

        [[nodiscard]] bool isRegister() const { return kind == Kind::REGISTER; }
        [[nodiscard]] bool isWire() const { return kind == Kind::WIRE; }
        [[nodiscard]] bool isInteger() const { return kind == Kind::INTEGER; }

        [[nodiscard]] bool isAlias() const { return alias; }

        [[nodiscard]] SignalIdxTy getIdx() const { return idx; }

        void dump(std::ostream &os) const;

        [[nodiscard]] size_t getObjectSize() const {
            return sizeof(*this) + name.size();
        }

      private:
        std::string name;
        SignalIdxTy idx;
        Kind kind;
        bool alias;
    };

    /// The Scope class provides a hierarchical view of the signals, and allows
    /// retrieving the index used to store the actual signals.
    class Scope {

      public:
        enum class Kind : uint8_t { MODULE, FUNCTION, TASK, BLOCK };

        Scope(std::string_view fullScopeName, std::string_view scopeName,
              std::string_view instanceName, Kind kind)
            : fullScopeName(fullScopeName), scopeName(scopeName),
              instanceName(instanceName), kind(kind), root(false) {}

        Scope()
            : fullScopeName("(root)"), scopeName("(root)"),
              instanceName("(root)"), kind(Kind::MODULE), root(true) {}

        Scope(Scope &&) = default;
        Scope(const Scope &other)
            : fullScopeName(other.fullScopeName), scopeName(other.scopeName),
              instanceName(other.instanceName), kind(other.kind),
              root(other.root) {
            subScopes.reserve(other.subScopes.size());
            for (const auto &s : other.subScopes)
                subScopes.emplace_back(new Scope(*s.get()));
            signals.reserve(other.signals.size());
            for (const auto &s : other.signals)
                signals.emplace_back(new SignalDesc(*s.get()));
        }

        Scope &operator=(Scope &&) = default;
        Scope &operator=(const Scope &rhs) {
            fullScopeName = rhs.fullScopeName;
            scopeName = rhs.scopeName;
            instanceName = rhs.instanceName;
            kind = rhs.kind;
            root = rhs.root;
            subScopes.clear();
            subScopes.reserve(rhs.subScopes.size());
            for (const auto &s : rhs.subScopes)
                subScopes.emplace_back(new Scope(*s.get()));
            signals.clear();
            signals.reserve(rhs.signals.size());
            for (const auto &s : rhs.signals)
                signals.emplace_back(new SignalDesc(*s.get()));
            return *this;
        }

        [[nodiscard]] bool isRoot() const { return root; }
        [[nodiscard]] const std::string &getScopeName() const {
            return scopeName;
        }
        [[nodiscard]] const std::string &getFullScopeName() const {
            return fullScopeName;
        }
        [[nodiscard]] const std::string &getInstanceName() const {
            return instanceName;
        }

        [[nodiscard]] bool isModule() const { return kind == Kind::MODULE; }
        [[nodiscard]] bool isTask() const { return kind == Kind::TASK; }
        [[nodiscard]] bool isFunction() const { return kind == Kind::FUNCTION; }
        [[nodiscard]] bool isBlock() const { return kind == Kind::BLOCK; }
        [[nodiscard]] Kind getKind() const { return kind; }

        [[nodiscard]] std::size_t getNumSubScopes() const {
            return subScopes.size();
        }
        [[nodiscard]] std::size_t getNumSignals() const {
            return signals.size();
        }

        [[nodiscard]] bool hasSubScopes() const { return !subScopes.empty(); }
        [[nodiscard]] bool hasSignals() const { return !signals.empty(); }

        [[nodiscard]] bool hasSubScope(const std::string &subScopeName) const {
            for (const auto &s : subScopes)
                if (s->instanceName == subScopeName)
                    return true;
            return false;
        }
        std::pair<bool, Scope *> findSubScope(const std::string &subScopeName) {
            for (auto &s : subScopes)
                if (s->instanceName == subScopeName)
                    return std::make_pair(true, s.get());
            return std::make_pair(false, nullptr);
        }
        [[nodiscard]] bool hasSignal(const std::string &signalName) const {
            for (const auto &s : signals)
                if (s->getName() == signalName)
                    return true;
            return false;
        }

        void dump(std::ostream &os, bool rec = true, unsigned level = 0) const {
            std::string WS(level * 4, ' ');
            os << WS << " - " << fullScopeName << " (";
            switch (kind) {
            case Kind::MODULE:
                os << "Module: " << scopeName;
                break;
            case Kind::TASK:
                os << "Task";
                break;
            case Kind::FUNCTION:
                os << "Function";
                break;
            case Kind::BLOCK:
                os << "Block";
                break;
            }
            os << "):\n";
            for (const auto &s : signals)
                os << WS << "   - " << s->getName() << '\n';
            for (const auto &s : subScopes) {
                os << WS << "   - " << s->instanceName;
                if (rec) {
                    os << ":\n";
                    s->dump(os, rec, level + 1);
                } else {
                    os << '\n';
                }
            }
        }

        [[nodiscard]] size_t getObjectSize() const {
            size_t size = sizeof(*this);
            size += fullScopeName.size();
            size += scopeName.size();
            size += instanceName.size();

            // Signals:
            size += signals.size() * sizeof(signals[0]);
            for (const auto &s : signals)
                size += s->getObjectSize();

            // SubScopes
            size += subScopes.size() * sizeof(subScopes[0]);
            for (const auto &s : subScopes)
                size += s->getObjectSize();

            return size;
        }

        /// Add a new Scope into this Scope.
        Scope &addScope(const std::string &instanceName,
                        const std::string &fullScopeName,
                        const std::string &scopeName, Kind kind) {
            const auto r = findSubScope(instanceName);
            if (r.first)
                return *r.second;
            subScopes.emplace_back(
                new Scope(fullScopeName, scopeName, instanceName, kind));
            return *subScopes.back().get();
        }

        /// Add a new Scope into this Scope (move edition).
        Scope &addScope(std::string &&instanceName, std::string &&fullScopeName,
                        std::string &&scopeName, Kind kind) {
            const auto r = findSubScope(instanceName);
            if (r.first)
                return *r.second;
            subScopes.emplace_back(new Scope(std::move(fullScopeName),
                                             std::move(scopeName),
                                             std::move(instanceName), kind));
            return *subScopes.back().get();
        }

        Scope &addModule(std::string &&instanceName,
                         std::string &&fullScopeName, std::string &&scopeName) {
            return addScope(std::move(instanceName), std::move(fullScopeName),
                            std::move(scopeName), Kind::MODULE);
        }
        Scope &addTask(std::string &&instanceName, std::string &&fullScopeName,
                       std::string &&scopeName) {
            return addScope(std::move(instanceName), std::move(fullScopeName),
                            std::move(scopeName), Kind::TASK);
        }
        Scope &addFunction(std::string &&instanceName,
                           std::string &&fullScopeName,
                           std::string &&scopeName) {
            return addScope(std::move(instanceName), std::move(fullScopeName),
                            std::move(scopeName), Kind::FUNCTION);
        }
        Scope &addBlock(std::string &&instanceName, std::string &&fullScopeName,
                        std::string &&scopeName) {
            return addScope(std::move(instanceName), std::move(fullScopeName),
                            std::move(scopeName), Kind::BLOCK);
        }

        void addSignal(const std::string &signalName, SignalDesc::Kind kind,
                       bool alias, SignalIdxTy idx) {
#ifndef NDEBUG
            if (hasSignal(signalName))
                DIE("Signal already exists in this Scope");
#endif
            signals.emplace_back(new SignalDesc(signalName, kind, alias, idx));
        }

        void addSignal(std::string &&signalName, SignalDesc::Kind kind,
                       bool alias, SignalIdxTy idx) {
#ifndef NDEBUG
            if (hasSignal(signalName))
                DIE("Signal already exists in this Scope");
#endif
            signals.emplace_back(
                new SignalDesc(std::move(signalName), kind, alias, idx));
        }

        [[nodiscard]] const SignalDesc &
        getSignalDesc(const std::string &signalName) const {
            for (const auto &s : signals)
                if (s->getName() == signalName)
                    return *s.get();
            DIE("Signal does not exist");
        }

        [[nodiscard]] SignalIdxTy
        getSignalIdx(const std::string &signalName) const {
            return getSignalDesc(signalName).getIdx();
        }

        [[nodiscard]] const SignalDesc *
        findSignalDesc(const std::string &FSN,
                       const std::string &signalName) const {
            if (fullScopeName == FSN) {
                // Yay, we are in the right scope !
                for (const auto &s : signals)
                    if (s->getName() == signalName)
                        return s.get();
                return nullptr;
            }

            if (root || FSN.size() > fullScopeName.size())
                for (const auto &s : subScopes) {
                    auto res = s->findSignalDesc(FSN, signalName);
                    if (res)
                        return res;
                }

            return nullptr;
        }

        [[nodiscard]] std::pair<bool, SignalIdxTy>
        findSignalIdx(const std::string &FSN,
                      const std::string &signalName) const {
            if (const SignalDesc *SD = findSignalDesc(FSN, signalName))
                return std::make_pair(true, SD->getIdx());

            return std::make_pair(false, -1);
        }

        /// Scope visitor base class.
        class Visitor {
          public:
            enum class FilterAction : uint8_t {
                SKIP_ALL,
                ENTER_SCOPE_ONLY,
                VISIT_ALL
            };

            class Options {
              public:
                Options(bool skipRegs = false, bool skipWires = false,
                        bool skipInts = false)
                    : skipRegs(skipRegs), skipWires(skipWires),
                      skipInts(skipInts) {}

                // Add a filter to select the scopes to visit. The filter
                Options &addScopeFilter(const std::string &filter) {
                    if (!filter.empty())
                        scopeFilters.push_back(filter);
                    return *this;
                }

                Options &setSkipRegisters(bool v) {
                    skipRegs = v;
                    return *this;
                }
                Options &setSkipWires(bool v) {
                    skipWires = v;
                    return *this;
                }
                Options &setSkipIntegers(bool v) {
                    skipInts = v;
                    return *this;
                }

                /// Returns true iff Signal \p S shall be skipped.
                [[nodiscard]] bool skip(const SignalDesc &SDesc) const {
                    switch (SDesc.getKind()) {
                    case SignalDesc::Kind::REGISTER:
                        return skipRegs;
                    case SignalDesc::Kind::WIRE:
                        return skipWires;
                    case SignalDesc::Kind::INTEGER:
                        return skipInts;
                    }
                }

                [[nodiscard]] bool isAllSkipped() const {
                    return skipRegs && skipWires && skipInts;
                }

                /// Returns false iff Scope \p scope shall be visited.
                [[nodiscard]] FilterAction filter(const Scope &scope) const;

              private:
                std::vector<std::string> scopeFilters;

                bool skipRegs;
                bool skipWires;
                bool skipInts;
            };

            // Visitor(const Options &options = Options()) : options(options) {}
            Visitor() = delete;
            Visitor(const Options &options = Options()) : options(options) {}

            Options &getOptions() { return options; }
            [[nodiscard]] const Options &getOptions() const { return options; }

            virtual ~Visitor() = default;
            virtual void enterScope(const Scope &scope) = 0;
            virtual void leaveScope() = 0;
            virtual void visitSignal(const std::string &fullScopeName,
                                     const SignalDesc &SDesc) = 0;

          private:
            Options options;
        };

        void accept(Visitor &V, Visitor::FilterAction act) const {
            if (act == Visitor::FilterAction::VISIT_ALL)
                for (const auto &s : signals) {
                    const SignalDesc &SD = *s.get();
                    if (!V.getOptions().skip(SD))
                        V.visitSignal(fullScopeName, SD);
                }
            for (const auto &s : subScopes) {
                const Scope &scope = *s.get();
                act = V.getOptions().filter(scope);
                if (act != Visitor::FilterAction::SKIP_ALL) {
                    V.enterScope(scope);
                    s->accept(V, act);
                    V.leaveScope();
                }
            }
        }

      private:
        std::string fullScopeName;
        std::string scopeName;
        std::string instanceName;
        std::vector<std::unique_ptr<Scope>> subScopes;
        std::vector<std::unique_ptr<SignalDesc>> signals;
        Kind kind;
        bool root;
    };

    Waveform() {}
    Waveform(std::string_view FileName) : fileName(FileName) {}
    Waveform(std::string_view FileName,
             uint64_t StartTime,
             uint64_t EndTime,
             signed char TimeScale)
        : fileName(FileName), startTime(StartTime), endTime(EndTime),
          timeScale(TimeScale) {}

    Waveform(const Waveform &W)
        : fileName(W.fileName), version(W.version), date(W.date),
          comment(W.comment), startTime(W.startTime), endTime(W.endTime),
          timeZero(W.timeZero), timeScale(W.timeScale), root(W.root),
          allTimes(W.allTimes) {
        signals.reserve(W.signals.size());
        for (const auto &s : W) {
            signals.emplace_back(new Signal(s));
            signals.back()->fixupTimeOrigin(&allTimes);
        }
    }
    Waveform(Waveform &&W) noexcept
        : fileName(std::move(W.fileName)), version(std::move(W.version)),
          date(std::move(W.date)), comment(std::move(W.comment)),
          startTime(W.startTime), endTime(W.endTime), timeZero(W.timeZero),
          timeScale(W.timeScale), root(std::move(W.root)),
          allTimes(std::move(W.allTimes)), signals(std::move(W.signals)) {
        for (auto &s : signals)
            s->fixupTimeOrigin(&allTimes);
    }
    Waveform &operator=(const Waveform &W) {
        fileName = W.fileName;
        version = W.version;
        date = W.date;
        comment = W.comment;
        startTime = W.startTime;
        endTime = W.endTime;
        timeZero = W.timeZero;
        timeScale = W.timeScale;
        root = W.root;
        allTimes = W.allTimes;
        signals.clear();
        signals.reserve(W.signals.size());
        for (const auto &s : W) {
            signals.emplace_back(new Signal(s));
            signals.back()->fixupTimeOrigin(&allTimes);
        }
        return *this;
    }
    Waveform &operator=(Waveform &&W) noexcept {
        fileName = std::move(W.fileName);
        version = std::move(W.version);
        date = std::move(W.date);
        comment = std::move(W.comment);
        startTime = W.startTime;
        endTime = W.endTime;
        timeZero = W.timeZero;
        timeScale = W.timeScale;
        root = std::move(W.root);
        allTimes = std::move(W.allTimes);
        signals = std::move(W.signals);
        for (auto &s : signals)
            s->fixupTimeOrigin(&allTimes);
        return *this;
    }

    [[nodiscard]] bool hasVersion() const { return !version.empty(); }
    [[nodiscard]] bool hasDate() const { return !date.empty(); }
    [[nodiscard]] bool hasComment() const { return !comment.empty(); }

    [[nodiscard]] const std::string &getFileName() const { return fileName; }
    [[nodiscard]] const std::string &getVersion() const { return version; }
    [[nodiscard]] const std::string &getDate() const { return date; }
    [[nodiscard]] const std::string &getComment() const { return comment; }
    [[nodiscard]] std::size_t getNumSignals() const { return signals.size(); }
    [[nodiscard]] uint64_t getStartTime() const { return startTime; }
    [[nodiscard]] uint64_t getEndTime() const { return endTime; }
    [[nodiscard]] int64_t getTimeZero() const { return timeZero; }
    [[nodiscard]] signed char getTimeScale() const { return timeScale; }
    signed char getTimeScale(std::string &ts) const;

    Waveform &setVersion(const std::string &v) {
        version = v;
        return *this;
    }
    Waveform &setDate(const std::string &d) {
        date = d;
        return *this;
    }
    Waveform &setComment(const std::string &text) {
        comment = text;
        return *this;
    }

    Waveform &setStartTime(int64_t t) {
        startTime = t;
        return *this;
    }
    Waveform &setStartTime() {
        startTime = allTimes.empty() ? 0 : allTimes[0];
        return *this;
    }

    Waveform &setEndTime(int64_t t) {
        endTime = t;
        return *this;
    }
    Waveform &setEndTime() {
        endTime = allTimes.empty() ? 0 : allTimes.back();
        return *this;
    }

    Waveform &setTimeZero(int64_t tz) {
        timeZero = tz;
        return *this;
    }

    Waveform &setTimeScale(signed char ts) {
        timeScale = ts;
        return *this;
    }

    // Populate Times with an ascending order sequence of times.
    template <class ForwardIt>
    Waveform &addTimes(ForwardIt begin, ForwardIt end) {
        assert(std::is_sorted(begin, end) &&
               "Times must be populated with a sorted sequence");
        if (!allTimes.empty())
            assert(allTimes.back() < *begin);
        allTimes.insert(allTimes.end(), begin, end);
        return *this;
    }

    Scope &addModule(std::string &&instanceName, std::string &&fullScopeName,
                     std::string &&scopeName) {
        return root.addModule(std::move(instanceName), std::move(fullScopeName),
                              std::move(scopeName));
    }
    Scope &addTask(std::string &&instanceName, std::string &&fullScopeName,
                   std::string &&scopeName) {
        return root.addTask(std::move(instanceName), std::move(fullScopeName),
                            std::move(scopeName));
    }
    Scope &addFunction(std::string &&instanceName, std::string &&fullScopeName,
                       std::string &&scopeName) {
        return root.addFunction(std::move(instanceName),
                                std::move(fullScopeName), std::move(scopeName));
    }
    Scope &addBlock(std::string &&instanceName, std::string &&fullScopeName,
                    std::string &&scopeName) {
        return root.addBlock(std::move(instanceName), std::move(fullScopeName),
                             std::move(scopeName));
    }

    // Create a new Signal.
    SignalIdxTy addSignal(Scope &S, std::string &&signalName, unsigned numBits,
                          SignalDesc::Kind k) {
        SignalIdxTy idx = signals.size();
        S.addSignal(std::move(signalName), k, false, idx);
        signals.emplace_back(new Signal(allTimes, numBits));
        return idx;
    }

    /// Create a Signal alias.
    SignalIdxTy addSignal(Scope &S, std::string &&signalName, unsigned numBits,
                          SignalDesc::Kind k, SignalIdxTy idx) {
        assert(idx < signals.size() && "idx is out of bounds");
        assert(signals[idx]->getNumBits() == numBits &&
               "Number of bits does not match with referenced signal");
        S.addSignal(std::move(signalName), k, true, idx);
        return idx;
    }

    // Add a new Signal.
    SignalIdxTy addSignal(Scope &S, std::string &&signalName,
                          SignalDesc::Kind k, const Signal &sig) {
        assert(sig.checkTimeOrigin(&allTimes) &&
               "Signal is using a different times");
        SignalIdxTy idx = signals.size();
        S.addSignal(std::move(signalName), k, false, idx);
        signals.emplace_back(new Signal(sig));
        return idx;
    }

    // Add a new Signal (move edition).
    SignalIdxTy addSignal(Scope &S, std::string &&signalName,
                          SignalDesc::Kind k, Signal &&sig) {
        assert(sig.checkTimeOrigin(&allTimes) &&
               "Signal is using a different times");
        SignalIdxTy idx = signals.size();
        S.addSignal(std::move(signalName), k, false, idx);
        signals.emplace_back(new Signal(std::move(sig)));
        return idx;
    }

    /// Add a new register signal.
    SignalIdxTy addRegister(Scope &S, std::string &&signalName,
                            unsigned numBits) {
        return addSignal(S, std::move(signalName), numBits,
                         SignalDesc::Kind::REGISTER);
    }
    /// Add a register alias to an exiting signal.
    SignalIdxTy addRegister(Scope &S, std::string &&signalName,
                            unsigned numBits, SignalIdxTy idx) {
        return addSignal(S, std::move(signalName), numBits,
                         SignalDesc::Kind::REGISTER, idx);
    }

    /// Add a new wire signal.
    SignalIdxTy addWire(Scope &S, std::string &&signalName, unsigned numBits) {
        return addSignal(S, std::move(signalName), numBits,
                         SignalDesc::Kind::WIRE);
    }
    /// Add a wire alias to an existing signal.
    SignalIdxTy addWire(Scope &S, std::string &&signalName, unsigned numBits,
                        WAN::SignalIdxTy idx) {
        return addSignal(S, std::move(signalName), numBits,
                         SignalDesc::Kind::WIRE, idx);
    }

    /// Add a new integer signal.
    SignalIdxTy addInteger(Scope &S, std::string &&signalName,
                           unsigned numBits) {
        return addSignal(S, std::move(signalName), numBits,
                         SignalDesc::Kind::INTEGER);
    }
    /// Add an integer alias to an existing signal.
    SignalIdxTy addInteger(Scope &S, std::string &&signalName, unsigned numBits,
                           WAN::SignalIdxTy idx) {
        return addSignal(S, std::move(signalName), numBits,
                         SignalDesc::Kind::INTEGER, idx);
    }

    [[nodiscard]] std::pair<bool, SignalIdxTy>
    findSignalIdx(const std::string &fullScopeName,
                  const std::string &signalName) const {
        return root.findSignalIdx(fullScopeName, signalName);
    }

    [[nodiscard]] const SignalDesc *
    findSignalDesc(const std::string &fullScopeName,
                   const std::string &signalName) const {
        return root.findSignalDesc(fullScopeName, signalName);
    }

    /// Add a change to Signal SIdx.
    Waveform &addValueChange(SignalIdxTy SIdx, const Signal::ChangeTy &c) {
        WAN::TimeIdxTy TIdx = addTime(c.time);
        signals[SIdx]->append(TIdx, c);
        return *this;
    }

    /// Add a change at time Time with value str to Signal SIdx.
    Waveform &addValueChange(SignalIdxTy SIdx, WAN::TimeTy Time,
                             const char *str) {
        WAN::TimeIdxTy TIdx = addTime(Time);
        signals[SIdx]->append(TIdx, str);
        return *this;
    }

    // Add a change at time Time with value str to Signal SIdx (string edition).
    Waveform &addValueChange(SignalIdxTy SIdx, WAN::TimeTy Time,
                             const std::string &str) {
        WAN::TimeIdxTy TIdx = addTime(Time);
        signals[SIdx]->append(TIdx, str);
        return *this;
    }

    void dump(std::ostream &os) const { root.dump(os, true); }
    void dumpMetadata(std::ostream &os) const;

    Scope *getRootScope() { return &root; }

    Signal &operator[](SignalIdxTy Idx) {
        return const_cast<Signal &>(static_cast<const Waveform &>(*this)[Idx]);
    }

    const Signal &operator[](SignalIdxTy Idx) const {
#ifndef NDEBUG
        if (Idx >= signals.size())
            DIE("Out of bound access");
#endif
        return *signals[Idx].get();
    }

    using signals_iterator = SignalsIterator<Signal>;
    signals_iterator begin() { return {&signals, 0}; }
    signals_iterator end() { return {&signals, signals.size()}; }
    using const_signals_iterator = SignalsIterator<Signal>;
    [[nodiscard]] const_signals_iterator begin() const { return {&signals, 0}; }
    [[nodiscard]] const_signals_iterator end() const {
        return {&signals, signals.size()};
    }

    using times_iterator = std::vector<WAN::TimeTy>::iterator;
    times_iterator timesBegin() { return allTimes.begin(); }
    times_iterator timesEnd() { return allTimes.end(); }
    using const_times_iterator = std::vector<WAN::TimeTy>::const_iterator;
    [[nodiscard]] const_times_iterator timesBegin() const {
        return allTimes.begin();
    }
    [[nodiscard]] const_times_iterator timesEnd() const {
        return allTimes.end();
    }

    /// Waveform visitor base class.
    class Visitor : public Scope::Visitor {
      public:
        Visitor(const Waveform *W, const Options &options = Options())
            : Scope::Visitor(options), w(W) {}

        [[nodiscard]] const Waveform *getWaveform() const { return w; }

      protected:
        const Waveform *w;
    };

    void visit(Visitor &V) const {
        root.accept(V, Visitor::FilterAction::ENTER_SCOPE_ONLY);
    }

    [[nodiscard]] size_t getObjectSize() const {
        size_t size = sizeof(*this);
        size += fileName.size();
        size += version.size();
        size += date.size();
        size += comment.size();
        size += allTimes.size() * sizeof(allTimes[0]);
        size += root.getObjectSize();
        size += signals.size() * sizeof(signals[0]);
        for (const auto &s : signals)
            size += s->getObjectSize();
        return size;
    }

  private:
    // The file from which those waves were read from.
    std::string fileName;
    // The file Version field.
    std::string version = "";
    // The file Date field.
    std::string date = "";
    // The file Comment field.
    std::string comment = "";
    // The waveform start time.
    uint64_t startTime = 0;
    // The waveform end time.
    uint64_t endTime = 0;
    // Offset to the simulation time.
    int64_t timeZero = 0;
    // The power of 10 in seconds, i.e. -12 = nano-seconds
    signed char timeScale = 0;

    Scope root;
    std::vector<WAN::TimeTy> allTimes;
    std::vector<std::unique_ptr<Signal>> signals;

    WAN::TimeIdxTy addTime(WAN::TimeTy Time) {
        // Times has to be kept sorted and in order, so that no index gets
        // broken.
        if (allTimes.empty() || Time > allTimes.back()) {
            allTimes.push_back(Time);
            return allTimes.size() - 1;
        }

        if (Time == allTimes.back())
            return allTimes.size() - 1;

        // Last chance, search for time inside Times.
        const auto it =
            std::lower_bound(allTimes.begin(), allTimes.end(), Time);
        if (it != allTimes.end() && *it == Time)
            return std::distance(allTimes.begin(), it);

        DIE("Can not add Time to Waveform, this would void all time indexes "
            "already used");
    }
};

class WaveformStatistics : public Waveform::Visitor {
  public:
    WaveformStatistics(const Waveform &W,
                       const Waveform::Visitor::Options &options =
                           Waveform::Visitor::Options())
        : Waveform::Visitor(&W, options) {}

    void enterScope(const Waveform::Scope &scope) override;
    void leaveScope() override;
    void visitSignal(const std::string &fullScopeName,
                     const Waveform::SignalDesc &SD) override;

    void dump(std::ostream &out) const;

  private:
    std::set<std::size_t> aliases;
    size_t numSignals{0};
    size_t numAliases{0};
    size_t numChanges{0};
    size_t timingsMemSize{}; //< Size in Bytes of the timing indexes.
    size_t signalsMemSize{
        0}; //< Size in Bytes in memory of the Waveform structure.
    size_t scopesMemSize{
        0}; //< Size in Bytes in memory of the Scopes structure.
};

std::ostream &operator<<(std::ostream &os, Waveform::SignalDesc::Kind k);

} // namespace PAF::WAN
