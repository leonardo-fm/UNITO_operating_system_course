int initializeSingalsHandlers();
int initializeConfig(char *configShareMemoryIdString);
int initializeBoat(char *boatIdS, char *shareMemoryIdS);
int work();
int newDay();
int gotoPort();
int setupTrade(int portId);
int openTrade();
int trade();
int closeTrade();
int haveIGoodsToSell();
int haveIGoodsToBuy();
int sellGoods();
int buyGoods();
int getSpaceAvailableInTheHold();
int cleanup();