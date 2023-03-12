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
#include <array>

#include <boost/asio/io_context.hpp>

#include <ready_trader_go/logging.h>

#include "autotrader.h"

using namespace ReadyTraderGo;

RTG_INLINE_GLOBAL_LOGGER_WITH_CHANNEL(LG_AT, "AUTO")

constexpr int MAX_ORDER_DEPTH = 5;
constexpr int LOT_SIZE = 10;
constexpr int POSITION_LIMIT = 100;
constexpr int TICK_SIZE_IN_CENTS = 100;
constexpr int MIN_BID_NEARST_TICK = (MINIMUM_BID + TICK_SIZE_IN_CENTS) / TICK_SIZE_IN_CENTS * TICK_SIZE_IN_CENTS;
constexpr int MAX_ASK_NEAREST_TICK = MAXIMUM_ASK / TICK_SIZE_IN_CENTS * TICK_SIZE_IN_CENTS;

Order MAX_ORDER = { MAXIMUM_ASK, 0, 0, false };
Order MIN_ORDER = { 0, 0, 0, false };

AutoTrader::AutoTrader(boost::asio::io_context& context) : BaseAutoTrader(context)
{
}

void AutoTrader::DisconnectHandler()
{
    BaseAutoTrader::DisconnectHandler();
    RLOG(LG_AT, LogLevel::LL_INFO) << "execution connection lost";
}

void AutoTrader::ErrorMessageHandler(unsigned long clientOrderId,
                                     const std::string& errorMessage)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "error with order " << clientOrderId << ": " << errorMessage;
    if (clientOrderId != 0 && ((mAsks.count(clientOrderId) == 1) || (mBids.count(clientOrderId) == 1)))
    {
        OrderStatusMessageHandler(clientOrderId, 0, 0, 0);
    }
}

void AutoTrader::HedgeFilledMessageHandler(unsigned long clientOrderId,
                                           unsigned long price,
                                           unsigned long volume)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "hedge order " << clientOrderId << " filled for " << volume
                                   << " lots at $" << price << " average price in cents";
}

void AutoTrader::OrderBookMessageHandler(Instrument instrument,
                                         unsigned long sequenceNumber,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes)
{
	
	if(sequenceNumber <= mOrderBookSequence) {
		RLOG(LG_AT, LogLevel::LL_INFO) << "received old order book information.";
		return;
	}
	mOrderBookSequence = sequenceNumber;
	
    RLOG(LG_AT, LogLevel::LL_INFO) << "order book received for " << instrument << " instrument"
                                   << ": ask prices: " << askPrices[0]
                                   << "; ask volumes: " << askVolumes[0]
                                   << "; bid prices: " << bidPrices[0]
                                   << "; bid volumes: " << bidVolumes[0];

    if (instrument != Instrument::FUTURE) {
        return;
    }
	
	RepriceSellOrders(askPrices, askVolumes);
	RepriceBuyOrders(bidPrices, bidVolumes);
	
}

void AutoTrader::RepriceSellOrders(const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& askPrices,
								  const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& askVolumes) {
		
	unsigned long newAskPrice = (askPrices[0] != 0) ? askPrices[0] + 100 : MAXIMUM_ASK;
	
	unsigned long largestOrderId = 0;
	Order* largestOrder = &MIN_ORDER;
	bool askAlreadyExists = false;
	int cc = 0;
	for(auto& [ orderId, order ] : mAsks) {
		if(order.cancelling) {
			cc++;
			continue;
		}
		if(order.price == newAskPrice) {
			askAlreadyExists = true;
		}
		if(order.price < newAskPrice) {
			SendCancelOrder(orderId);
			order.cancelling = true;
		} else if(order.price >= largestOrder->price) {
			largestOrderId = orderId;
			largestOrder = &order;
		}
	}
	
	RLOG(LG_AT, LogLevel::LL_INFO) << cc << " already cancelling rn (there are " << mAsks.size() << " total)";
	
	if(largestOrderId != 0 && !largestOrder->cancelling && mETFOrderAskCount >= MAX_ORDER_DEPTH - 1) {
		RLOG(LG_AT, LogLevel::LL_INFO) << "cancelling sell order " << largestOrderId << " @ " << largestOrder->price << " to make room for other orders";
		largestOrder->cancelling = true;
		SendCancelOrder(largestOrderId);
	}
	
	if (askAlreadyExists 
		|| (mETFPosition - mETFOrderPositionSell - LOT_SIZE) < -POSITION_LIMIT 
		|| mETFOrderAskCount >= MAX_ORDER_DEPTH
		|| askPrices[0] == 0) {
		return;
	}
	
	auto orderId = mNextMessageId++;
    SendInsertOrder(orderId, Side::SELL, newAskPrice, LOT_SIZE, Lifespan::GOOD_FOR_DAY);

	mETFOrderAskCount++;
    mETFOrderPositionSell += LOT_SIZE;
    
	mAsks[orderId] = { newAskPrice, LOT_SIZE, 0 };
	
}

void AutoTrader::RepriceBuyOrders(const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& bidPrices,
								  const std::array<unsigned long, ReadyTraderGo::TOP_LEVEL_COUNT>& bidVolumes) {
	
	unsigned long newBidPrice = (bidPrices[0] != 0) ? bidPrices[0] - 100 : 0;
	
	unsigned long smallestOrderId = 0;
	Order* smallestOrder = &MAX_ORDER;
	bool bidAlreadyExists = false;
	for(auto& [ orderId, order ] : mBids) {
		if(order.cancelling) {
			continue;
		}
		if(order.price == newBidPrice) {
			bidAlreadyExists = true;
		}
		if(order.price > newBidPrice) {
			SendCancelOrder(orderId);
			order.cancelling = true;
		} else if(order.price <= smallestOrder->price) {
			smallestOrderId = orderId;
			smallestOrder = &order;
		}
	}
	
	if(smallestOrderId != 0 && !smallestOrder->cancelling && mETFOrderBidCount >= MAX_ORDER_DEPTH - 1) {
		RLOG(LG_AT, LogLevel::LL_INFO) << "cancelling buy order poo" << smallestOrderId << " @ " << smallestOrder->price << " to make room for other orders";
		smallestOrder->cancelling = true;
		SendCancelOrder(smallestOrderId);
	} else {
		RLOG(LG_AT, LogLevel::LL_INFO) << "not cancelling buy order " << smallestOrderId << " " << mETFOrderBidCount;	
	}
	
	if(bidAlreadyExists 
		|| (mETFPosition + mETFOrderPositionBuy + LOT_SIZE) > POSITION_LIMIT 
		|| mETFOrderBidCount >= MAX_ORDER_DEPTH
		|| bidPrices[0] == 0) {
		return;
	}
	
	auto orderId = mNextMessageId++;
    SendInsertOrder(orderId, Side::BUY, newBidPrice, LOT_SIZE, Lifespan::GOOD_FOR_DAY);

	mETFOrderBidCount++;
    mETFOrderPositionBuy += LOT_SIZE;
    mBids[orderId] = { newBidPrice, LOT_SIZE, 0 };
	
}

void AutoTrader::OrderFilledMessageHandler(unsigned long clientOrderId,
                                           unsigned long price,
                                           unsigned long volume)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "order filled message " << clientOrderId << " " << price << " " << volume;
}

void AutoTrader::OrderStatusMessageHandler(unsigned long clientOrderId,
                                           unsigned long fillVolume,
                                           unsigned long remainingVolume,
                                           signed long fees)
{

    RLOG(LG_AT, LogLevel::LL_INFO) << "order status message received "
        << clientOrderId << " " << fillVolume << " " << remainingVolume << " " << fees;

    bool isSellOrder = mAsks.count(clientOrderId);
    if(!isSellOrder && !mBids.count(clientOrderId)) {
        RLOG(LG_AT, LogLevel::LL_INFO) << "received order status for order we are not tracking. id=" << clientOrderId;
        return;
    }

    auto& sideMap = isSellOrder ? mAsks : mBids;
    Order& order = sideMap[clientOrderId];

    // Update our futures position to make sure we are correctly hedged
    auto dFilled = fillVolume - order.filledVolume;
    if(dFilled > 0) {
        mETFPosition += isSellOrder ? -dFilled : dFilled;
        SendHedgeOrder(mNextMessageId++,
                       isSellOrder ? Side::BUY : Side::SELL,
                       isSellOrder ? MAX_ASK_NEAREST_TICK : MIN_BID_NEARST_TICK, dFilled);
    }

    // Update the state
    auto dRemaining = order.remainingVolume - remainingVolume;
    if(isSellOrder) {
        mETFOrderPositionSell -= dRemaining;
    } else {
        mETFOrderPositionBuy -= dRemaining;
    }

    if(remainingVolume > 0) {
        order.remainingVolume = remainingVolume;
        order.filledVolume = fillVolume;
    } else {
        if(isSellOrder) {
			mETFOrderAskCount--;
		} else {
			mETFOrderBidCount--;
		}
        sideMap.erase(clientOrderId);
    }

}

void AutoTrader::TradeTicksMessageHandler(Instrument instrument,
                                          unsigned long sequenceNumber,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "trade ticks received for " << instrument << " instrument"
                                   << ": ask prices: " << askPrices[0]
                                   << "; ask volumes: " << askVolumes[0]
                                   << "; bid prices: " << bidPrices[0]
                                   << "; bid volumes: " << bidVolumes[0];
}