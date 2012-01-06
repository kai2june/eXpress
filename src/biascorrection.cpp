/*
 *  biascorrection.h
 *  express
 *
 *  Created by Adam Roberts on 4/5/11.
 *  Copyright 2011 Adam Roberts. All rights reserved.
 *
 */

#include <assert.h>
#include <algorithm>
#include <boost/assign.hpp>
#include "main.h"
#include "biascorrection.h"
#include "transcripts.h"
#include "fragments.h"
#include "frequencymatrix.h"

using namespace std;

const vector<size_t> LEN_BINS = boost::assign::list_of(791)(1265)(1707)(2433)(999999999); 
const vector<double> POS_BINS = boost::assign::list_of(0.1)(0.2)(0.3)(0.4)(0.5)(0.6)(0.7)(0.8)(0.9)(1.0);

const int SURROUND = 10;
const int CENTER = 11;
const int WINDOW = 21;
const string PADDING = "NNNNNNNNNN"; 

SeqWeightTable::SeqWeightTable(size_t window_size, double alpha)
:_observed(window_size, NUM_NUCS, alpha),
 _expected(1, NUM_NUCS, 0, false) 
{}

void SeqWeightTable::increment_expected(char c)
{
    WriteLock lock(_lock);
    size_t index = ctoi(c);
    if (index != 4)
    {
        _expected.increment(index, 1);
    }
}

void SeqWeightTable::normalize_expected()
{
    _expected.set_logged(true);
}

void SeqWeightTable::increment_observed(string& seq, double normalized_mass)
{
    WriteLock lock(_lock);
    for (size_t i = 0; i < seq.length(); ++i)
    {
        size_t index = ctoi(seq[i]);
        if (index != 4)
            _observed.increment(i, index, normalized_mass);
    }
}

double SeqWeightTable::get_weight(const string& seq, int i) const
{
    ReadLock lock(_lock);
    double weight = 0;
    for (int j = max(0, CENTER-1-i); j < min(WINDOW, CENTER-1+(int)seq.length()-i); ++j)
    {
        size_t index = ctoi(seq[i+j-CENTER+1]);
        if (index != 4)
            weight += _observed(j, index) - _expected(index);
    }
    return weight;
}

string SeqWeightTable::to_string() const
{
    ReadLock lock(_lock);
    char buffer[50];
    string s = "";
    for(int i = 0; i < WINDOW; ++i)
    {
        for (size_t j = 0; j < NUM_NUCS; ++j)
        {
            sprintf(buffer, "%e ", sexp(_observed(i, j)));
            s += buffer;
        }
    }
    s.erase(s.length()-1,1);
    return s;
}

void SeqWeightTable::append_output(ofstream& outfile) const
{
    char buff[200];
    string header = "";
    for(int i = 0; i < WINDOW; i++)
    {
        sprintf(buff, "\t%d", i-CENTER);
        header += buff;
    }
    header += '\n';
    
    outfile << "\tObserved Nucleotide Distribution\n" << header;
    
    ReadLock lock(_lock);
    for(size_t j = 0; j < NUM_NUCS; j++)
    {
        outfile << NUCS[j] << ":\t";
        for(int i = 0; i < WINDOW; i++)
        {
            outfile << scientific << sexp(_observed(i,j)) << "\t";
        }
        outfile<<endl;
    }
    
    outfile << "\tBias Weights\n" << header;
    
    for(size_t j = 0; j < NUM_NUCS; j++)
    {
        outfile << NUCS[j] << ":\t";
        for(int i = 0; i < WINDOW; i++)
        {
            outfile << scientific << sexp(_observed(i,j) - _expected(j)) << "\t";
        }
        outfile<<endl;
    }

}

PosWeightTable::PosWeightTable(const vector<size_t>& len_bins, const vector<double>& pos_bins, double alpha)
:_observed(FrequencyMatrix(len_bins.size(), pos_bins.size(),alpha)),
 _expected(FrequencyMatrix(len_bins.size(), pos_bins.size(),0,false)),
 _len_bins(len_bins),
 _pos_bins(pos_bins)
{}

void PosWeightTable::increment_expected(size_t len, double pos)
{
    size_t l = upper_bound(len_bins().begin(),len_bins().end(), len) - len_bins().begin();
    size_t p = upper_bound(pos_bins().begin(),pos_bins().end(), pos) - pos_bins().begin();
    increment_expected(l,p);
}

void PosWeightTable::increment_expected(size_t l, size_t p)
{
    WriteLock lock(_lock);
    _expected.increment(l, p, 1);
}

void PosWeightTable::normalize_expected()
{
    _expected.set_logged(true);
}

void PosWeightTable::increment_observed(size_t len, double pos, double normalized_mass)
{
    size_t l = upper_bound(len_bins().begin(),len_bins().end(), len) - len_bins().begin();
    size_t p = upper_bound(pos_bins().begin(),pos_bins().end(), pos) - pos_bins().begin();
    increment_observed(l,p, normalized_mass);
} 

void PosWeightTable::increment_observed(size_t l, size_t p, double normalized_mass)
{
    WriteLock lock(_lock);
    _observed.increment(l,p, normalized_mass);
} 

double PosWeightTable::get_weight(size_t len, double pos) const
{
    size_t l = upper_bound(len_bins().begin(),len_bins().end(), len) - len_bins().begin();
    size_t p = upper_bound(pos_bins().begin(),pos_bins().end(), pos) - pos_bins().begin();
    ReadLock lock(_lock);
    return _observed(l,p)-_expected(l,p);
}

double PosWeightTable::get_weight(size_t l, size_t p) const
{
    ReadLock lock(_lock);
    return _observed(l,p)-_expected(l,p);
}

void PosWeightTable::append_output(ofstream& outfile) const
{
    char buff[200];
    string header = "";

    
    sprintf(buff, "\t%0.2f-%0.2f", 0.0, pos_bins()[0]);
    header += buff;
    for(size_t p = 1; p < pos_bins().size(); p++)
    {
        sprintf(buff, "\t%0.2f-%0.2f", pos_bins()[p-1], pos_bins()[p]);
        header += buff;
    }
    header += '\n';
        
    outfile << "\tObserved Position Distribution\n" << header;

    ReadLock lock(_lock);
    sprintf(buff, "%d-" SIZE_T_FMT ":\t", 0, len_bins()[0]);
    for(size_t l = 0; l < len_bins().size(); l++)
    {
        if(l)
            sprintf(buff, "" SIZE_T_FMT "-" SIZE_T_FMT ":\t", len_bins()[l-1]+1, len_bins()[l]);
        outfile << buff;
        for(size_t p = 0; p < pos_bins().size(); p++)
        {
            outfile << scientific << sexp(_observed(l,p)) << "\t";
        }
        outfile<<endl;
    }
    
    outfile << "\tBias Weights\n" << header;
    
    sprintf(buff, "%d-" SIZE_T_FMT ":\t", 0, len_bins()[0]);
    for(size_t l = 0; l < len_bins().size(); l++)
    {
        if(l)
            sprintf(buff, "" SIZE_T_FMT "-" SIZE_T_FMT ":\t", len_bins()[l-1]+1, len_bins()[l]);
        outfile << buff;
        for(size_t p = 0; p < pos_bins().size(); p++)
        {
            outfile << scientific << sexp(_observed(l,p)-_expected(l,p)) << "\t";
        }
        outfile<<endl;
    }

}

BiasBoss::BiasBoss(double alpha)
: _5_seq_bias(WINDOW, alpha),
  _3_seq_bias(WINDOW, alpha),
  _5_pos_bias(LEN_BINS, POS_BINS, alpha),
  _3_pos_bias(LEN_BINS, POS_BINS, alpha)
{}

void BiasBoss::update_expectations(const Transcript& trans)
{
    
    size_t l = upper_bound(_5_pos_bias.len_bins().begin(),_5_pos_bias.len_bins().end(), trans.length()) - _5_pos_bias.len_bins().begin();
    size_t p = 0;
    double next_bin_start = trans.length() * _5_pos_bias.pos_bins()[p];
    const string& seq = trans.seq();
    for (size_t i = 0; i < seq.length(); ++i)
    {
        if (i >= next_bin_start)
        {
            next_bin_start = trans.length() * _5_pos_bias.pos_bins()[++p];
        }
        _5_pos_bias.increment_expected(l,p);
        _3_pos_bias.increment_expected(l,p);
        _5_seq_bias.increment_expected(seq[i]);
        _3_seq_bias.increment_expected(seq[i]);
    }
}

void BiasBoss::normalize_expectations()
{
    _5_seq_bias.normalize_expected();
    _3_seq_bias.normalize_expected();
    _5_pos_bias.normalize_expected();
    _3_pos_bias.normalize_expected();
}

void BiasBoss::update_observed(const FragHit& hit, double normalized_mass)
{
    assert (hit.pair_status() != PAIRED || hit.length() > WINDOW);
    
    const string t_seq = hit.mapped_trans->seq();
    
    if (hit.pair_status() != RIGHT_ONLY)
    {
        string seq_5;
        int left_window = (int)hit.left - (CENTER-1);
        if (left_window < 0)
        {
            seq_5 = PADDING.substr(0, -left_window);
            seq_5 += t_seq.substr(0, WINDOW + left_window);
        }
        else
        {
            seq_5 = t_seq.substr(left_window, WINDOW); 
        }
        
        _5_seq_bias.increment_observed(seq_5, normalized_mass);
        _5_pos_bias.increment_observed(t_seq.length(), (double)hit.left/t_seq.length(), normalized_mass);
    }
    
    if (hit.pair_status() != LEFT_ONLY)
    {
        int left_window = (int)hit.right - CENTER;
        assert(left_window < t_seq.length());
        string seq_3 = t_seq.substr(left_window, WINDOW);
        int overhang =left_window + WINDOW - (int)t_seq.length();
        if (overhang > 0)
        {
            seq_3 += PADDING.substr(0, overhang);
        }
        
        _3_seq_bias.increment_observed(seq_3, normalized_mass);
        _3_pos_bias.increment_observed(t_seq.length(), (double)(hit.right-1)/t_seq.length(), normalized_mass);
    }
}

double BiasBoss::get_transcript_bias(std::vector<float>& start_bias, std::vector<float>& end_bias, const Transcript& trans) const
{
    double tot_start = HUGE_VAL;
    double tot_end = HUGE_VAL;
    
    size_t l = upper_bound(_5_pos_bias.len_bins().begin(),_5_pos_bias.len_bins().end(), trans.length()) - _5_pos_bias.len_bins().begin();
    
    size_t p = 0;
    double next_bin_start = trans.length() * _5_pos_bias.pos_bins()[p];
    double curr_5_pos_bias = _5_pos_bias.get_weight(l,p);
    double curr_3_pos_bias = _3_pos_bias.get_weight(l,p);
    for (size_t i = 0; i < trans.length(); ++i)
    {
        if (i >= next_bin_start)
        {
            next_bin_start = trans.length() * _5_pos_bias.pos_bins()[++p];
            curr_5_pos_bias = _5_pos_bias.get_weight(l,p);
            curr_3_pos_bias = _3_pos_bias.get_weight(l,p);
        }
        start_bias[i] = _5_seq_bias.get_weight(trans.seq(), (int)i) + curr_5_pos_bias;
        end_bias[i] = _3_seq_bias.get_weight(trans.seq(), (int)i) + curr_3_pos_bias;
        tot_start = log_sum(tot_start, start_bias[i]);
        tot_end = log_sum(tot_start, end_bias[i]);
    }
    
    double avg_bias = (tot_start + tot_end) - (2*log((double)trans.length()));
    return avg_bias;
}

string BiasBoss::to_string() const
{
    return _5_seq_bias.to_string();
}

void BiasBoss::append_output(ofstream& outfile) const
{
    outfile<<">5' Sequence-Specific Bias\n";
    _5_seq_bias.append_output(outfile);
    outfile<<">3' Sequence-Specific Bias\n";
    _3_seq_bias.append_output(outfile);
    outfile<<">5' Fractional Position Bias\n";
    _5_pos_bias.append_output(outfile);
    outfile<<">3' Fractional Position Bias\n";
    _3_pos_bias.append_output(outfile);
}

