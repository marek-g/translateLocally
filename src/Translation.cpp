#include "Translation.h"
#include "3rd_party/bergamot-translator/src/translator/response.h"
#include <QDebug>

namespace {

bool contains(marian::bergamot::ByteRange const &span, std::size_t offset) {
    return offset >= span.begin && offset < span.end;
}

auto findWordByByteOffset(marian::bergamot::Annotation const &annotation, std::size_t pos) {
    std::size_t sentenceIdx, wordIdx;

    for (sentenceIdx = 0; sentenceIdx < annotation.numSentences() - 1; ++sentenceIdx)
        if (annotation.sentence(sentenceIdx).end > pos) // annotations are sorted, so this should be safe to assume.
            break;

    // if not found, sentenceIdx == numSentences() - 1 (i.e. the last sentence)
    
    for (wordIdx = 0; wordIdx < annotation.numWords(sentenceIdx) - 1; ++wordIdx)
        if (annotation.word(sentenceIdx, wordIdx).end >= pos)
            break;

    // Same. worst case, wordIdx == annotation.numWords(sentenceIdx) - 1

    return std::tuple(sentenceIdx, wordIdx);
}

int offsetToPosition(std::string const &text, std::size_t offset) {
    if (offset > text.size())
        return -1;

    return QString::fromUtf8(text.data(), offset).size();
}

std::size_t positionToOffset(std::string const &text, int pos) {
    return QString::fromStdString(text).left(pos).toLocal8Bit().size();
}

auto _source(marian::bergamot::Response const &response, Translation::Direction direction) {
    return direction == Translation::source_to_translation ? response.source : response.target;
}

auto _target(marian::bergamot::Response &response, Translation::Direction direction) {
    return direction == Translation::source_to_translation ? response.target : response.source;
}

} // Anonymous namespace

Translation::Translation()
: response_()
, speed_(-1) {
    //
}

Translation::Translation(marian::bergamot::Response &&response, int speed)
: response_(std::make_shared<marian::bergamot::Response>(std::move(response)))
, speed_(speed) {
    //
}

QString Translation::translation() const {
    return QString::fromStdString(response_->target.text);
}

QList<WordAlignment> Translation::alignments(Direction direction, int sourcePosFirst, int sourcePosLast) const {
    QList<WordAlignment> alignments;
    std::size_t sentenceIdxFirst, sentenceIdxLast, wordIdxFirst, wordIdxLast;

    if (!response_)
        return alignments;

    if (sourcePosFirst > sourcePosLast)
        std::swap(sourcePosFirst, sourcePosLast);

    std::size_t sourceOffsetFirst = ::positionToOffset(::_source(*response_, direction).text, sourcePosFirst);
    std::tie(sentenceIdxFirst, wordIdxFirst) = ::findWordByByteOffset(::_source(*response_, direction).annotation, sourceOffsetFirst);

    std::size_t sourceOffsetLast = ::positionToOffset(::_source(*response_, direction).text, sourcePosLast);
    std::tie(sentenceIdxLast, wordIdxLast) = ::findWordByByteOffset(::_source(*response_, direction).annotation, sourceOffsetLast);

    assert(sentenceIdxFirst <= sentenceIdxLast);
    assert(sentenceIdxFirst != sentenceIdxLast || wordIdxFirst <= wordIdxLast);
    assert(sentenceIdxLast < response_->alignments.size());

    for (std::size_t sentenceIdx = sentenceIdxFirst; sentenceIdx <= sentenceIdxLast; ++sentenceIdx) {
        assert(sentenceIdx < response_->alignments.size());
        std::size_t firstWord = sentenceIdx == sentenceIdxFirst ? wordIdxFirst : 0;
        std::size_t lastWord = sentenceIdx == sentenceIdxLast ? wordIdxLast : ::_source(*response_, direction).numWords(sentenceIdx) - 1;
        for (marian::bergamot::Point const &point : response_->alignments[sentenceIdx]) {
            if (direction == source_to_translation ? (point.src >= firstWord && point.src <= lastWord) : (point.tgt >= firstWord && point.tgt <= lastWord)) {
                auto span = ::_target(*response_, direction).wordAsByteRange(sentenceIdx, direction == source_to_translation ? point.tgt : point.src);
                WordAlignment alignment;
                alignment.begin = ::offsetToPosition(::_target(*response_, direction).text, span.begin);
                alignment.end = ::offsetToPosition(::_target(*response_, direction).text, span.end);
                alignment.prob = point.prob;
                alignments.append(alignment);
            }
        }
    }

    std::sort(alignments.begin(), alignments.end(), [](WordAlignment const &a, WordAlignment const &b) {
        return a.begin < b.begin && a.prob > b.prob;
    });

    return alignments;
}
