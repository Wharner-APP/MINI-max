#pragma once
#include <QString>

#include "core/payments.h"

class PopupHost;

namespace Pages {
void settings(PopupHost *h);
void premium(PopupHost *h, bool push = true);
void stars(PopupHost *h, bool push = true);
void business(PopupHost *h);
void giftPeople(PopupHost *h);
void giftShop(PopupHost *h, const QString &recipientName, const QString &recipientLogin);
void chatProfile(PopupHost *h, int chatRow);
void myProfile(PopupHost *h);
void autoDeleteTimer(PopupHost *h, int chatRow);
void startPayment(PopupHost *h, const PaymentRequest &req);
void infoDialog(PopupHost *h, const QString &title, const QString &text);
void contacts(PopupHost *h);
void calls(PopupHost *h);
void createGroup(PopupHost *h, bool channel);
}
