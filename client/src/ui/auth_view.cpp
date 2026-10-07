#include "ui/auth_view.h"

#include <QAction>
#include <QBuffer>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QVBoxLayout>
#include <mm/build_info.h>

#include "core/api.h"
#include "core/crypto.h"
#include "core/device_info.h"
#include "core/install_markers.h"
#include "ui/icons.h"
#include "ui/popup.h"
#include "ui/theme.h"

namespace {

// Puzzle captcha: background + movable piece. Emits the final X (in background pixel coordinates).
class CaptchaWidget : public QWidget {
    Q_OBJECT
public:
    CaptchaWidget(QWidget *parent = nullptr) : QWidget(parent) {
        auto *l = new QVBoxLayout(this);
        l->setContentsMargins(24, 20, 24, 16);
        l->setSpacing(10);
        auto *t = new QLabel("Подтвердите, что вы не робот");
        QFont f = font();
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() + 1.5);
        t->setFont(f);
        l->addWidget(t);
        m_hint = new QLabel("Передвиньте ползунок, чтобы собрать пазл.");
        m_hint->setWordWrap(true);
        m_hint->setStyleSheet(QString("color:%1;").arg(pal().textSecondary.name()));
        l->addWidget(m_hint);
        m_canvas = new QLabel;
        m_canvas->setFixedSize(312, 156);
        m_canvas->setAlignment(Qt::AlignCenter);
        m_canvas->setStyleSheet(QString("background:%1; border-radius:8px;").arg(pal().sectionBg.name()));
        l->addWidget(m_canvas, 0, Qt::AlignHCenter);
        m_slider = new QSlider(Qt::Horizontal);
        m_slider->setRange(0, 1000);
        m_slider->setEnabled(false);
        l->addWidget(m_slider);
        auto *bl = new QHBoxLayout;
        bl->addStretch(1);
        auto *cancel = new QPushButton("Отмена");
        cancel->setObjectName("flat");
        bl->addWidget(cancel);
        l->addLayout(bl);
        connect(cancel, &QPushButton::clicked, this, &CaptchaWidget::cancelled);
        connect(m_slider, &QSlider::valueChanged, this, [this] { redraw(); });
        connect(m_slider, &QSlider::sliderReleased, this, [this] {
            if (m_bg.isNull()) return;
            m_slider->setEnabled(false);
            m_hint->setText("Проверка…");
            emit solved(m_id, positionX());
        });
    }
    void load(ApiClient *api) {
        m_api = api;
        m_slider->setValue(0);
        m_slider->setEnabled(false);
        m_bg = m_piece = QImage();
        m_canvas->setText("Загрузка…");
        api->get("/api/get_captcha", this, [this](const ApiResult &r) {
            if (!r.ok) { m_canvas->setText(""); m_hint->setText("Не удалось загрузить капчу: " + r.error); emit failedToLoad(); return; }
            m_id = r.json["captcha_id"].toString();
            m_pieceY = r.json["piece_y"].toInt();
            fetchImage(r.json["background"].toString(), [this](const QImage &img) { m_bg = img; ready(); });
            fetchImage(r.json["piece"].toString(), [this](const QImage &img) { m_piece = img; ready(); });
        }, true);
    }
    void setHint(const QString &t) { m_hint->setText(t); }
signals:
    void solved(const QString &captchaId, int x);
    void cancelled();
    void failedToLoad();
private:
    void fetchImage(const QString &src, std::function<void(const QImage &)> cb) {
        if (src.startsWith("data:")) {
            cb(QImage::fromData(QByteArray::fromBase64(src.mid(src.indexOf(',') + 1).toLatin1())));
            return;
        }
        m_api->download(src, this, [cb](const QByteArray &d, const QString &) { cb(QImage::fromData(d)); });
    }
    void ready() {
        if (m_bg.isNull() || m_piece.isNull()) return;
        m_canvas->setText({});
        m_hint->setText("Передвиньте ползунок, чтобы собрать пазл.");
        m_slider->setEnabled(true);
        redraw();
    }
    int maxX() const { return m_bg.width() - m_piece.width(); }
    int positionX() const { return maxX() * m_slider->value() / 1000; }
    void redraw() {
        if (m_bg.isNull() || m_piece.isNull()) return;
        QImage canvas = m_bg.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QPainter p(&canvas);
        p.drawImage(positionX(), m_pieceY, m_piece);
        p.end();
        m_canvas->setPixmap(QPixmap::fromImage(canvas).scaled(m_canvas->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    ApiClient *m_api = nullptr;
    QLabel *m_hint, *m_canvas;
    QSlider *m_slider;
    QImage m_bg, m_piece;
    QString m_id;
    int m_pieceY = 0;
};

QLineEdit *field(const QString &placeholder, bool password = false) {
    auto *e = new QLineEdit;
    e->setPlaceholderText(placeholder);
    e->setMinimumHeight(44);
    e->setStyleSheet(QString("QLineEdit { border-radius: 8px; border: 1px solid %1; padding: 8px 12px; background: transparent; } QLineEdit:focus { border: 2px solid %2; padding: 7px 11px; }")
                         .arg(pal().textSecondary.darker(160).name(), pal().accent.name()));
    if (password) e->setEchoMode(QLineEdit::Password);
    return e;
}

}  // namespace

#include "auth_view.moc"

AuthView::AuthView(QWidget *parent) : QWidget(parent) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addStretch(1);
    auto *card = new QWidget;
    card->setFixedWidth(380);
    auto *l = new QVBoxLayout(card);
    l->setSpacing(10);
    l->setContentsMargins(0, 0, 0, 0);

    m_logo = new QLabel;
    m_logo->setAlignment(Qt::AlignCenter);
    QIcon logo(":/logo.ico");
    if (!logo.isNull() && !logo.availableSizes().isEmpty()) m_logo->setPixmap(logo.pixmap(96, 96));
    else {
        QPixmap pm(192, 192);
        pm.setDevicePixelRatio(2);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient g(0, 0, 96, 96);
        g.setColorAt(0, QColor("#5eb5f7"));
        g.setColorAt(1, QColor("#3d7fb5"));
        p.setBrush(g);
        p.setPen(Qt::NoPen);
        p.drawEllipse(0, 0, 96, 96);
        p.setPen(Qt::white);
        QFont f;
        f.setPixelSize(44);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(0, 0, 96, 96), Qt::AlignCenter, "M");
        m_logo->setPixmap(pm);
    }
    l->addWidget(m_logo);
    m_title = new QLabel("MINI max");
    QFont tf = font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() + 8);
    m_title->setFont(tf);
    m_title->setAlignment(Qt::AlignCenter);
    l->addWidget(m_title);
    m_subtitle = new QLabel("Войдите в аккаунт или создайте новый");
    m_subtitle->setAlignment(Qt::AlignCenter);
    m_subtitle->setStyleSheet(QString("color:%1;").arg(pal().textSecondary.name()));
    l->addWidget(m_subtitle);
    l->addSpacing(8);

    auto *tabs = new QHBoxLayout;
    m_tabLogin = new QPushButton("Вход");
    m_tabReg = new QPushButton("Регистрация");
    for (auto *b : {m_tabLogin, m_tabReg}) { b->setCursor(Qt::PointingHandCursor); b->setMinimumHeight(38); tabs->addWidget(b); }
    l->addLayout(tabs);

    m_login = field("Логин");
    m_name = field("Отображаемое имя");
    m_pass = field("Пароль", true);
    m_pass2 = field("Подтвердите пароль", true);
    m_regOnly1 = m_name;
    m_regOnly2 = m_pass2;
    for (QLineEdit *e : {m_login, m_name, m_pass, m_pass2}) l->addWidget(e);
    for (QLineEdit *e : {m_pass, m_pass2}) {
        QAction *a = e->addAction(Icons::icon("eye", pal().textSecondary, 20), QLineEdit::TrailingPosition);
        connect(a, &QAction::triggered, this, [e, a] {
            const bool hidden = e->echoMode() == QLineEdit::Password;
            e->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
            a->setIcon(Icons::icon(hidden ? "eye-off" : "eye", pal().textSecondary, 20));
        });
    }
    m_error = new QLabel;
    m_error->setWordWrap(true);
    m_error->setStyleSheet(QString("color:%1;").arg(pal().danger.name()));
    m_error->hide();
    l->addWidget(m_error);
    m_submit = new QPushButton;
    m_submit->setObjectName("primary");
    m_submit->setMinimumHeight(46);
    m_submit->setCursor(Qt::PointingHandCursor);
    l->addWidget(m_submit);
    m_disclosure = new QLabel(
        QString("Чтобы защитить сервис от повторных аккаунтов, при регистрации приложение запишет небольшие служебные файлы-метки в несколько папок "
                "вашего профиля и передаст серверу идентификаторы устройства: MAC-адрес, IP-адрес и сведения об оборудовании. Сообщения на сервер не передаются. "
                "<a href='%1' style='color:%2'>Подробнее</a>").arg(MM_WEBSITE, pal().link.name()));
    m_disclosure->setWordWrap(true);
    m_disclosure->setOpenExternalLinks(true);
    QFont df = font();
    df.setPointSizeF(df.pointSizeF() - 1.5);
    m_disclosure->setFont(df);
    m_disclosure->setStyleSheet(QString("color:%1;").arg(pal().textSecondary.name()));
    l->addWidget(m_disclosure);

    auto *row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(card);
    row->addStretch(1);
    outer->addLayout(row);
    outer->addStretch(1);
    m_status = new QLabel;
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setContentsMargins(0, 0, 0, 12);
    outer->addWidget(m_status);

    m_popup = new PopupHost(this);
    connect(m_tabLogin, &QPushButton::clicked, this, [this] { showTab(false); });
    connect(m_tabReg, &QPushButton::clicked, this, [this] { showTab(true); });
    connect(m_submit, &QPushButton::clicked, this, &AuthView::submit);
    for (QLineEdit *e : {m_login, m_name, m_pass, m_pass2}) connect(e, &QLineEdit::returnPressed, this, &AuthView::submit);
    showTab(false);
    setStatus("Подключение к серверу…", false);
}

void AuthView::showTab(bool reg) {
    m_register = reg;
    const Palette &c = pal();
    auto style = [&](bool active) {
        return QString("QPushButton { background: transparent; border: none; border-bottom: 2px solid %1; color: %2; font-weight: bold; }")
            .arg(active ? c.accent.name() : "transparent", active ? c.link.name() : c.textSecondary.name());
    };
    m_tabLogin->setStyleSheet(style(!reg));
    m_tabReg->setStyleSheet(style(reg));
    m_regOnly1->setVisible(reg);
    m_regOnly2->setVisible(reg);
    m_disclosure->setVisible(reg);
    m_submit->setText(reg ? "РЕГИСТРАЦИЯ" : "ВОЙТИ");
    m_error->hide();
}

void AuthView::setStatus(const QString &text, bool ok) {
    m_status->setText(QString("<span style='color:%1'>●</span>&nbsp; %2").arg(ok ? "#4fae4e" : "#e0a030", text.toHtmlEscaped()));
}

void AuthView::setBusy(bool b) {
    m_submit->setEnabled(!b);
    for (QLineEdit *e : {m_login, m_name, m_pass, m_pass2}) e->setEnabled(!b);
}

void AuthView::showError(const QString &t) { m_error->setText(t); m_error->setVisible(!t.isEmpty()); }

QString AuthView::validate() const {
    static const QRegularExpression re("^[A-Za-z0-9_.]{3,32}$");
    if (!re.match(m_login->text().trimmed()).hasMatch()) return "Логин: 3–32 символа, латинские буквы, цифры, «_» и «.».";
    if (!m_register) return m_pass->text().isEmpty() ? "Введите пароль." : QString();
    if (m_name->text().trimmed().isEmpty() || m_name->text().trimmed().size() > 40) return "Введите отображаемое имя (до 40 символов).";
    if (m_pass->text().size() < 8) return "Пароль должен содержать не менее 8 символов.";
    if (m_pass->text() != m_pass2->text()) return "Пароли не совпадают.";
    return {};
}

void AuthView::submit() {
    showError({});
    const QString err = validate();
    if (!err.isEmpty()) { showError(err); return; }
    if (m_register) doRegister(); else doLogin();
}

void AuthView::finishAuth(const ApiResult &r, const QByteArray &localKey, bool created) {
    const QJsonObject u = r.json["user"].toObject();
    Session s;
    s.token = r.json["token"].toString();
    s.userId = qint64(u["id"].toDouble());
    s.login = u["login"].toString(m_login->text().trimmed());
    s.displayName = u["display_name"].toString(m_name->text().trimmed());
    s.bio = u["bio"].toString();
    if (s.token.isEmpty()) { showError("Сервер вернул некорректный ответ."); setBusy(false); return; }
    if (created) InstallMarkers::create(s.login, DeviceInfo::collect().fingerprint);
    m_pass->clear();
    m_pass2->clear();
    setBusy(false);
    emit authenticated(s, localKey);
}

void AuthView::doLogin() {
    setBusy(true);
    m_submit->setText("Вход…");
    const auto keys = mmcrypto::deriveFromPassword(m_login->text(), m_pass->text());
    AppContext::i().api->post("/api/login", {{"login", m_login->text().trimmed()}, {"auth_key", QString::fromLatin1(keys.authKey.toHex())}}, this,
                              [this, keys](const ApiResult &r) {
        m_submit->setText("ВОЙТИ");
        if (!r.ok) { showError(r.status == 0 ? "Нет связи с сервером. Проверьте файл connect.ip." : r.error); setBusy(false); return; }
        finishAuth(r, keys.localKey, false);
    });
}

void AuthView::openCaptcha(std::function<void(const QString &, int, std::function<void(bool)>)> onSolved) {
    auto *cw = new CaptchaWidget;
    m_popup->open(cw, {}, 360);
    cw->load(AppContext::i().api);
    connect(cw, &CaptchaWidget::cancelled, this, [this] { m_popup->closeAll(); setBusy(false); m_submit->setText("РЕГИСТРАЦИЯ"); });
    connect(cw, &CaptchaWidget::failedToLoad, this, [this] { setBusy(false); m_submit->setText("РЕГИСТРАЦИЯ"); });
    connect(cw, &CaptchaWidget::solved, this, [this, cw, onSolved](const QString &id, int x) {
        onSolved(id, x, [this, cw](bool retry) {
            if (retry) { cw->setHint("Не получилось. Попробуйте ещё раз."); cw->load(AppContext::i().api); }
            else { m_popup->closeAll(); }
        });
    });
}

void AuthView::doRegister() {
    setBusy(true);
    m_submit->setText("Проверка устройства…");
    const DeviceInfo dev = DeviceInfo::collect();
    auto fail = [this](const QString &t) { showError(t); setBusy(false); m_submit->setText("РЕГИСТРАЦИЯ"); };
    if (InstallMarkers::present()) { fail("С этого компьютера уже создан аккаунт. Повторная регистрация невозможна."); return; }
    AppContext::i().api->post("/api/device/check", {{"fingerprint", dev.fingerprint}, {"macs", QJsonArray::fromStringList(dev.macs)}}, this,
                              [this, fail, dev](const ApiResult &r) {
        if (!r.ok) { fail(r.status == 0 ? "Нет связи с сервером. Проверьте файл connect.ip." : r.error); return; }
        if (r.json["registered"].toBool()) { fail("С этого компьютера уже создан аккаунт. Повторная регистрация невозможна."); return; }
        const auto keys = mmcrypto::deriveFromPassword(m_login->text(), m_pass->text());
        openCaptcha([this, keys, dev](const QString &id, int x, std::function<void(bool)> done) {
            QJsonObject body{{"login", m_login->text().trimmed()}, {"display_name", m_name->text().trimmed()},
                             {"auth_key", QString::fromLatin1(keys.authKey.toHex())}, {"captcha_id", id}, {"captcha_x", x}, {"device", dev.toJson()}};
            AppContext::i().api->post("/api/register", body, this, [this, keys, done](const ApiResult &r) {
                if (r.ok) { done(false); m_submit->setText("РЕГИСТРАЦИЯ"); finishAuth(r, keys.localKey, true); return; }
                if (r.json["error"].toString() == "captcha_failed") { done(true); return; }
                done(false);
                showError(r.status == 0 ? "Нет связи с сервером. Проверьте файл connect.ip." : r.error);
                setBusy(false);
                m_submit->setText("РЕГИСТРАЦИЯ");
            });
        });
    });
}

void AuthView::startCaptchaForTest() {
    openCaptcha([](const QString &, int, std::function<void(bool)>) {});
}
