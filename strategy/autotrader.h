// Copyright 2021 Optiver Asia Pacific Pty. Ltd.
//
// This file is part of Ready Trader Go.
//
//     Ready Trader Go is free software: you can redistribute it and/or
//     modify it under the terms of the GNU Affero General Public License
//     as published by the Free Software Foundation, either version 3 of
//     the License, or (at your option) any later version.
//
//     Ready Trader Go is distributed in the hope that it will be useful,
//     but WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//     GNU Affero General Public License for more details.
//
//     You should have received a copy of the GNU Affero General Public
//     License along with Ready Trader Go.  If not, see
//     <https://www.gnu.org/licenses/>.
#ifndef CPPREADY_TRADER_GO_AUTOTRADER_H
#define CPPREADY_TRADER_GO_AUTOTRADER_H

#include <array>
#include <memory>
#include <string>
#include <unordered_set>

#include <boost/asio/io_context.hpp>

#include <ready_trader_go/baseautotrader.h>
#include <ready_trader_go/types.h>

struct Order {

    unsigned long price;

    unsigned long remainingVolume;
    unsigned long filledVolume;

    bool cancelling = false;
};

class AutoTrader : public ReadyTraderGo::BaseAutoTrader {
public:
    explicit AutoTrader(boost::asio::io_context &context);

    // Called when the execution connection is lost.
    void DisconnectHandler() override;

    // Called when the matching engine detects an error.
    // If the error pertains to a particular order, then the client_order_id
    // will identify that order, otherwise the client_order_id will be zero.
    void ErrorMessageHandler(unsigned long clientOrderId,
                             const std::string &errorMessage) override;

    // Called when one of your hedge orders is filled, partially or fully.
    //
    // The price is the average price at which the order was (partially) filled,
    // which may be better than the order's limit price. The volume is
    // the number of lots filled at that price.
    //
    // If the order was unsuccessful, both the price and volume will be zero.
    void HedgeFilledMessageHandler(unsigned long clientOrderId,
                                   unsigned long price,
                                   unsigned long volume) override;

    // Called periodically to report the status of an order book.
    // The sequence number can be used to detect missed or out-of-order
    // messages. The five best available ask (i.e. sell) and bid (i.e. buy)
    // prices are reported along with the volume available at each of those
    // price levels.
    void OrderBookMessageHandler(
        ReadyTraderGo::Instrument instrument, unsigned long sequenceNumber,
        const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>
            &askPrices,
        const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>
            &askVolumes,
        const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>
            &bidPrices,
        const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>
            &bidVolumes) override;

    void RepriceBuyOrders(unsigned long newBidPrice);
    void RepriceSellOrders(unsigned long newAskPrice);

    // Called when one of your orders is filled, partially or fully.
    void OrderFilledMessageHandler(unsigned long clientOrderId,
                                   unsigned long price,
                                   unsigned long volume) override;

    // Called when the status of one of your orders changes.
    // The fill volume is the number of lots already traded, remaining volume
    // is the number of lots yet to be traded and fees is the total fees paid
    // or received for this order.
    // Remaining volume will be set to zero if the order is cancelled.
    void OrderStatusMessageHandler(unsigned long clientOrderId,
                                   unsigned long fillVolume,
                                   unsigned long remainingVolume,
                                   signed long fees) override;

    // Called periodically when there is trading activity on the market.
    // The five best ask (i.e. sell) and bid (i.e. buy) prices at which there
    // has been trading activity are reported along with the aggregated volume
    // traded at each of those price levels.
    // If there are less than five prices on a side, then zeros will appear at
    // the end of both the prices and volumes arrays.
    void TradeTicksMessageHandler(
        ReadyTraderGo::Instrument instrument, unsigned long sequenceNumber,
        const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>
            &askPrices,
        const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>
            &askVolumes,
        const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>
            &bidPrices,
        const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>
            &bidVolumes) override;

private:
    unsigned long mNextMessageId = 1;
    unsigned long mOrderBookSequence = 0;

    // The change in the position we hold if all orders that have left our bot
    // were filled either mETFPosition + mETFOrderPositionBuy > 100 or
    // mETFPosition - mETFOrderPositionSell < 100 will disqualify our bot
    signed long mETFOrderPositionSell = 0;
    signed long mETFOrderPositionBuy = 0;

    unsigned int mETFOrderAskCount = 0;
    unsigned int mETFOrderBidCount = 0;

    signed long mETFPosition = 0;

    // We track the state of our orders that are currently in the market
    std::unordered_map<unsigned long, Order> mAsks;
    std::unordered_map<unsigned long, Order> mBids;
};

#endif // CPPREADY_TRADER_GO_AUTOTRADER_H
