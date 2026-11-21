import sys,unittest,numpy as np
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from slopscope_usb.analysis import Processor,measure,validate_processing
class AnalysisTests(unittest.TestCase):
 def test_square_wave_and_noise(self):
  a=np.where((np.arange(2000)+139)%500<250,18,-90)
  r=measure(a,500000);self.assertAlmostEqual(r['frequency_hz'],1000,places=6);self.assertAlmostEqual(r['duty_percent'],50,places=3);self.assertIsNone(r['rise_s'])
  self.assertIsNone(measure(np.random.default_rng(1).normal(-40,1,2000),500000)['frequency_hz'])
 def test_partial_cycle_not_period(self):
  a=np.r_[np.ones(500)*20,np.ones(500)*-80];self.assertIsNone(measure(a,500000)['frequency_hz'])
 def test_average_peak_and_reset(self):
  p=Processor();p.configure('mode','average');p.configure('averages',2);m={'ch1_range':9,'ch2_range':9,'ch1_coupling':0,'ch2_coupling':0,'offsets_packed':0}
  for x in (10,20,30):r=p.process([[x]*20,[x]*20],1000,m)
  self.assertEqual(r['display_channels'][0][0],25)
  m['ch1_range']=10;r=p.process([[50]*20]*2,1000,m);self.assertEqual(r['processing_count'],1)
  p.configure('mode','peak');p.process([[10]*20]*2,1000,m);r=p.process([[30]*20]*2,1000,m);self.assertEqual(r['envelope']['low'][0][0],10);self.assertEqual(r['envelope']['high'][0][0],30)
 def test_fft_coherent_gain_and_divide_zero(self):
  p=Processor();p.configure('math','fft');m={'ch1_range':9,'ch2_range':9,'ch1_coupling':0,'ch2_coupling':0,'offsets_packed':0};a=30*np.sin(2*np.pi*np.arange(2000)*1000/500000)
  r=p.process([a,np.zeros(2000)],500000,m)['math'];i=np.argmax(r['values']);self.assertEqual(r['frequencies'][i],1000);self.assertAlmostEqual(r['values'][i],30,delta=.03)
  p.configure('math','divide');self.assertEqual(p.process([a,np.zeros(2000)],500000,m)['math']['values'],[None]*2000)
 def test_validation(self):
  for key,value in [('averages',True),('averages',257),('math','unknown'),('intensity',0)]:
   with self.assertRaises(ValueError):validate_processing(key,value)
if __name__=='__main__':unittest.main()
