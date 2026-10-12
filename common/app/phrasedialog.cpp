#include "phrasedialog.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "widgets.h"

PhraseDialog::PhraseDialog(const QStringList& words, QWidget* parent) : QDialog(parent) {
    setWindowTitle("Recovery phrase");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(18);
    auto* title = new QLabel("Recovery phrase");
    title->setFont(Theme::font(1.45, QFont::Bold));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);
    auto* description = label("These words restore the wallet and everything in it. Write them down in order and keep them offline; anyone who "
                              "sees them can take the funds.",
                              Theme::Tone::secondary);
    description->setWordWrap(true);
    description->setAlignment(Qt::AlignCenter);
    description->setFixedWidth(420);
    layout->addLayout(centered(description));
    auto* card = new Card;
    auto* grid = new QGridLayout(card);
    grid->setContentsMargins(16, 16, 16, 16);
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(10);
    for (int i = 0; i < words.size(); ++i) {
        auto* cell = new QWidget;
        auto* cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(6);
        auto* number = label(QString("%1.").arg(i + 1), Theme::Tone::secondary);
        number->setFixedWidth(24);
        number->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        cellLayout->addWidget(number);
        auto* word = new QLabel(words[i]);
        word->setFont(Theme::mono(1.08));
        word->setMinimumWidth(96);
        cellLayout->addWidget(word);
        grid->addWidget(cell, i / 3, i % 3);
    }
    layout->addWidget(card, 0, Qt::AlignHCenter);
    auto* done = new QPushButton("Done");
    done->setDefault(true);
    done->setMinimumWidth(96);
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(done, 0, Qt::AlignHCenter);
    setFixedWidth(520);
}
