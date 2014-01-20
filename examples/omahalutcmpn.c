/*
 *  omahacmpn.c: a program to compare up to 9 pairs of omaha hole cards at any
 *           point of the game (pre-flop, on the flop, turn or river).
 *              
 *  Example:
 *
 *      omahacmpn  kh tc ac 10h  3h ah 9h 3d  5h 6h 2h 2d -- 8c 6h 7h
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program in a file in the toplevel directory called "GPLv3".
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "poker_defs.h"
#include "inlines/eval_omaha.h"

#define MAX_PLAYERS 9

int monteCarlo = 0;
int gNCommon, gNDead, gNPlayers, gNIter;
CardMask gDeadCards, gCommonCards, gPlayerCards[MAX_PLAYERS];

int getCard(CardMask c) {
    /*
      2c =  1    2d =  2    2h =  3    2s =  4
      3c =  5    3d =  6    3h =  7    3s =  8
      4c =  9    4d = 10    4h = 11    4s = 12
      5c = 13    5d = 14    5h = 15    5s = 16
      6c = 17    6d = 18    6h = 19    6s = 20
      7c = 21    7d = 22    7h = 23    7s = 24
      8c = 25    8d = 26    8h = 27    8s = 28
      9c = 29    9d = 30    9h = 31    9s = 32
      Tc = 33    Td = 34    Th = 35    Ts = 36
      Jc = 37    Jd = 38    Jh = 39    Js = 40
      Qc = 41    Qd = 42    Qh = 43    Qs = 44
      Kc = 45    Kd = 46    Kh = 47    Ks = 48
      Ac = 49    Ad = 50    Ah = 51    As = 52
    */

    int i;
    StdDeck.maskToCards(&c, &i);
    return (4 * StdDeck_RANK(i)) + StdDeck_SUIT(i) + 1;
}

static void
parseArgs(int argc, char **argv) {
    int i, seenSep = 0, cardCount = 0, c;

    for (i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-d") == 0) {
                if (++i == argc) goto error;
                if (StdDeck_stringToCard(argv[i], &c) == 0)
                    goto error;
                if (!CardMask_CARD_IS_SET(gDeadCards, c)) {
                    ++gNDead;
                    StdDeck_CardMask_SET(gDeadCards, c);
                };
            } 
            else if (strcmp(argv[i], "-m") == 0) {
                if (++i == argc) goto error;
                gNIter = strtol(argv[i], NULL, 10);
                if (gNIter == EINVAL || gNIter == ERANGE) {
                    printf("bad option m: %s\n", argv[i]);
                    goto error;
                }
                monteCarlo = 1;
            }
            else if (!strcmp(argv[i], "--"))
                seenSep = 1;
            else {
                printf("bad option: %s\n", argv[i]);
                goto error;
            }
        } else {
            if (StdDeck_stringToCard(argv[i], &c) == 0) {
                printf("bad card: %s\n", argv[i]);
                goto error;
            }
            if (seenSep) {
                StdDeck_CardMask_SET(gCommonCards, c);
                ++gNCommon;
            }
            else {
                int i = cardCount / 4;
                StdDeck_CardMask_SET(gPlayerCards[i], c);
                gNPlayers = i+1;
                ++cardCount;
            };
        }
    }
    if (gNPlayers < 2 || gNPlayers > 9) {
        printf("bad number of players: %d\n", gNPlayers);
        goto error;
    }
    if (gNCommon > 5) {
        printf("bad number of common cards: %d\n", gNCommon);
        goto error;
    }

    return;

  error:
    fprintf(stderr, "Usage: omahacmpn [ -m <iterations> ] [ -d dead-card ] p1-cards .. p9-cards [ -- common-cards ]\n");
    exit(0);
}


int main( int argc, char *argv[] )
{
    CardMask cards, deadCards, board;
    HandVal handval[MAX_PLAYERS], bestHand;
    LowHandVal lowhandval[MAX_PLAYERS], bestLowHand;
    int i;
    unsigned long winCount[MAX_PLAYERS], loseCount[MAX_PLAYERS], 
        tieCount[MAX_PLAYERS], handCount=0;
    float ev[MAX_PLAYERS];
    int tiedPlayerIndexes[MAX_PLAYERS];
    int tiedLowPlayerIndexes[MAX_PLAYERS];
    int isTie = 0;
    int isLowTie = 0;
    int numTies = 0, numLowTies = 0;
    int bestHiIndex = -1, bestLoIndex = -1;

    CardMask_RESET(gCommonCards);
    CardMask_RESET(gDeadCards);
  
    for (i=0; i<MAX_PLAYERS; i++) {
        CardMask_RESET(gPlayerCards[i]);
        winCount[i] = 0;
        tieCount[i] = 0;
        loseCount[i] = 0;
        ev[i] = 0;
    };
    parseArgs(argc, argv);

    deadCards = gDeadCards;
    for (i=0; i<gNPlayers; i++) {
        CardMask_OR(deadCards, deadCards, gPlayerCards[i]);
    };

    if (!monteCarlo) {
        ENUMERATE_N_CARDS_D(cards, 5-gNCommon, deadCards, 
                            {
                                ++handCount;
                                bestHand = HandVal_NOTHING;
                                bestLowHand = LowHandVal_NOTHING;
                                for (i=0; i<gNPlayers; i++) {
                                    CardMask_OR(board, gCommonCards, cards);
                                    int ret = StdDeck_OmahaHiLow8_EVAL_LUT(gPlayerCards[i], board, &handval[i], &lowhandval[i]);
                                    if (ret) {
                                        printf("Error calculating OmahaHi: %d\n", ret);
                                        exit(-1);
                                    }
                                    if (handval[i] > bestHand) {
                                        bestHand = handval[i];
                                        isTie = 0;
                                        numTies = 0;
                                        bestHiIndex = i;
                                    }
                                    // Otherwise, if this hand ties another, adjust some state...
                                    else if (handval[i] == bestHand)	{
                                        if (numTies == 0) {
                                            tiedPlayerIndexes[0] = bestHiIndex;
                                            tiedPlayerIndexes[1] = i;
                                        }
                                        else {
                                            tiedPlayerIndexes[numTies + 1] = i;
                                        }

                                        isTie = 1;
                                        numTies++;
                                    }

                                    // If this hand is the best low hand we've seen so far, adjust some state...
                                    if (lowhandval[i] != LowHandVal_NOTHING && lowhandval[i] < bestLowHand) {
                                        bestLowHand = lowhandval[i];
                                        isLowTie = 0;
                                        numLowTies = 0;;
                                        bestLoIndex = i;
                                    }
                                    
                                    // Otherwise, if this hand ties another low, add ties
                                    else if (lowhandval[i] != LowHandVal_NOTHING && lowhandval[i] == bestLowHand) {
                                        if (numLowTies == 0) {
                                            tiedLowPlayerIndexes[0] = bestLoIndex;
                                            tiedLowPlayerIndexes[1] = i;
                                        }
                                        else {
                                            tiedPlayerIndexes[numLowTies + 1] = i;
                                        }
                                        
                                        isLowTie = 1;
                                        numLowTies++;
                                    }
                                }

                                // max part of pot any player can win
                                float maxPotSize = 1.0f;
                                if (bestLowHand != LowHandVal_NOTHING)
                                    maxPotSize = 0.5f; // split pot
                                
                                // divide up the high half
                                if (!isTie) {
                                    // besthi gets the maxPotSize for hi side, could be all
                                    ev[bestHiIndex] += maxPotSize;
                                    if (maxPotSize == 1.0f)
                                        ++winCount[bestHiIndex];
                                    else
                                        ++tieCount[bestHiIndex];
                                }
                                else {
                                    // split the high half between ties
                                    double partialHiWin = maxPotSize / ((double)numTies + 1.0f);
                                    for (int i = 0; i <= numTies; i++) {
                                        ev[ tiedPlayerIndexes[i] ] += partialHiWin;
                                        ++tieCount[ tiedPlayerIndexes[i] ];
                                    }
                                }
                                
                                // if there is a low side of the pot, divide the low half
                                if (bestLowHand != LowHandVal_NOTHING) {
                                    if (!isLowTie) {
                                        // bestlo gets half the pot
                                        ev[bestLoIndex] += maxPotSize;
                                        ++tieCount[ bestLoIndex ];
                                    }
                                    else {
                                        // split the low half between ties
                                        double partialLoWin = maxPotSize / ((double)numLowTies + 1.0f);
                                        for (int i = 0; i <= numLowTies; i++) {
                                            ev[ tiedLowPlayerIndexes[i] ] += partialLoWin;
                                            ++tieCount[ tiedPlayerIndexes[i] ];
                                        }
                                    }
                                }
                            }
                            );
    }
    else {
        /* Monte-Carlo simulation */
        DECK_MONTECARLO_N_CARDS_D(StdDeck, cards, deadCards, 5-gNCommon, gNIter,
                                  {
                                      ++handCount;
                                      bestHand = HandVal_NOTHING;
                                      bestLowHand = LowHandVal_NOTHING;
                                      for (i=0; i<gNPlayers; i++) {
                                          CardMask_OR(board, gCommonCards, cards);
                                          int ret = StdDeck_OmahaHiLow8_EVAL_LUT(gPlayerCards[i], board, &handval[i], &lowhandval[i]);
                                          if (ret) {
                                              printf("Error calculating OmahaHi: %d\n", ret);
                                              exit(-1);
                                          }
                                          if (handval[i] > bestHand) {
                                              bestHand = handval[i];
                                              isTie = 0;
                                              numTies = 0;
                                              bestHiIndex = i;
                                          }
                                          // Otherwise, if this hand ties another, adjust some state...
                                          else if (handval[i] == bestHand)	{
                                              if (numTies == 0) {
                                                  tiedPlayerIndexes[0] = bestHiIndex;
                                                  tiedPlayerIndexes[1] = i;
                                              }
                                              else {
                                                  tiedPlayerIndexes[numTies + 1] = i;
                                              }
                                              
                                              isTie = 1;
                                              numTies++;
                                          }
                                          
                                          // If this hand is the best low hand we've seen so far, adjust some state...
                                          if (lowhandval[i] != LowHandVal_NOTHING && lowhandval[i] < bestLowHand) {
                                              bestLowHand = lowhandval[i];
                                              isLowTie = 0;
                                              numLowTies = 0;;
                                              bestLoIndex = i;
                                          }
                                          
                                          // Otherwise, if this hand ties another low, add ties
                                          else if (lowhandval[i] != LowHandVal_NOTHING && lowhandval[i] == bestLowHand) {
                                              if (numLowTies == 0) {
                                                  tiedLowPlayerIndexes[0] = bestLoIndex;
                                                  tiedLowPlayerIndexes[1] = i;
                                              }
                                              else {
                                                  tiedPlayerIndexes[numLowTies + 1] = i;
                                              }
                                              
                                              isLowTie = 1;
                                              numLowTies++;
                                          }
                                      }
                                      
                                      // max part of pot any player can win
                                      float maxPotSize = 1.0f;
                                      if (bestLowHand != LowHandVal_NOTHING)
                                          maxPotSize = 0.5f; // split pot
                                      
                                      // divide up the high half
                                      if (!isTie) {
                                          // besthi gets the maxPotSize for hi side, could be all
                                          ev[bestHiIndex] += maxPotSize;
                                          if (maxPotSize == 1.0f)
                                              ++winCount[bestHiIndex];
                                          else
                                              ++tieCount[bestHiIndex];
                                      }
                                      else {
                                          // split the high half between ties
                                          double partialHiWin = maxPotSize / ((double)numTies + 1.0f);
                                          for (int i = 0; i <= numTies; i++) {
                                              ev[ tiedPlayerIndexes[i] ] += partialHiWin;
                                              ++tieCount[ tiedPlayerIndexes[i] ];
                                          }
                                      }
                                      
                                      // if there is a low side of the pot, divide the low half
                                      if (bestLowHand != LowHandVal_NOTHING) {
                                          if (!isLowTie) {
                                              // bestlo gets half the pot
                                              ev[bestLoIndex] += maxPotSize;
                                              ++tieCount[ bestLoIndex ];
                                          }
                                          else {
                                              // split the low half between ties
                                              double partialLoWin = maxPotSize / ((double)numLowTies + 1.0f);
                                              for (int i = 0; i <= numLowTies; i++) {
                                                  ev[ tiedLowPlayerIndexes[i] ] += partialLoWin;
                                                  ++tieCount[ tiedPlayerIndexes[i] ];
                                              }
                                          }
                                      }
                                  }
                                  );
    }
    printf("%ld boards", handCount);
    if (gNCommon > 0) 
        printf(" containing %s ", Deck_maskString(gCommonCards));
    if (gNDead) 
        printf(" with %s removed ", Deck_maskString(gDeadCards));
    printf("\n");

    printf("  cards            win  %%win       loss  %%lose       tie  %%tie      EV\n");
    for (i=0; i<gNPlayers; i++) 
        printf("  %s  %7ld %6.2f   %7ld %6.2f   %7ld %6.2f     %6.2f%%\n", 
               Deck_maskString(gPlayerCards[i]), 
               winCount[i], 100.0*winCount[i]/handCount, 
               loseCount[i], 100.0*loseCount[i]/handCount, 
               tieCount[i], 100.0*tieCount[i]/handCount, 
               (ev[i] / handCount) * 100.0);

    return 0;
}