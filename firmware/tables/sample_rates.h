/* Extracted stock model sample clocks; nominal Hz interprets table as MHz. */
const unsigned sample_rates[][4]={
 {8u,1u,1000000000u,8u},
 {4u,1u,500000000u,8u},
 {2u,1u,250000000u,9u},
 {1u,1u,100000000u,10u},
 {1u,2u,50000000u,11u},
 {1u,4u,25000000u,12u},
 {1u,10u,10000000u,13u},
 {1u,20u,5000000u,14u},
 {1u,40u,2500000u,15u},
 {1u,100u,1000000u,16u},
 {1u,200u,500000u,17u},
 {1u,400u,250000u,18u},
 {1u,1000u,100000u,19u},
 {1u,2000u,50000u,20u},
 {1u,4000u,25000u,21u},
 {1u,10000u,10000u,22u},
 {1u,20000u,5000u,23u},
 {1u,40000u,2500u,24u},
 {1u,100000u,1000u,25u},
 {1u,200000u,500u,26u},
 {1u,400000u,250u,27u},
 {1u,1000000u,100u,28u},
 {1u,2000000u,50u,29u},
 {1u,4000000u,25u,30u},
 {1u,10000000u,10u,31u},
 {1u,20000000u,5u,32u},
 {1u,40000000u,5u,33u},
 {1u,100000000u,1u,34u},
 {1u,200000000u,1u,35u},
 {1u,400000000u,1u,36u},
 {1u,1000000000u,1u,37u},
 {1u,2000000000u,1u,38u},
};
#define SCOPE_RATE_COUNT 32u
unsigned sample_rate_denominator(unsigned rate){return rate==26||rate==28?2:rate==29?4:rate==30?10:rate==31?20:1;}
unsigned sample_rate_numerator(unsigned rate){return sample_rates[rate][2];}

