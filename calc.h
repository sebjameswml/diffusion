#include <vector>
#include <array>
#include <sstream>
#include <iostream>
#include <morph/RD_Base.h>
#include <morph/vvec.h>
#include <functional>


/*Inherit methods and attributes from the RD_base*/
template <typename Flt>
class D_calc : public morph::RD_Base<Flt>
{
    public:
    alignas(alignof(std::vector<Flt>))

    morph::vvec<Flt> THflux;
    morph::vvec<Flt> Fflux;
    morph::vvec<Flt> total_flux;
    morph::vvec<Flt> T; //temperature

    alignas(Flt) Flt D_Fflux = 0.1;
    alignas(Flt) Flt D_THflux = 0.1;
    alignas(Flt) Flt zeroPointValue = 0.0;
    alignas(bool) bool doNoise = false;
    alignas(Flt) Flt noiseHeight = 1;

    alignas(Flt) Flt k1 = 1.0;
    alignas(Flt) Flt k2 = 1.0;
    alignas(Flt) Flt k3 = 1.0;
    alignas(Flt) Flt k4 = 1.0;

    D_calc() : morph::RD_Base<Flt>() {}


    void allocate()
    {
        morph::RD_Base<Flt>::allocate();

        this->resize_vector_variable (this->Fflux);
        this->resize_vector_variable (this->THflux);
        this->resize_vector_variable (this->total_flux);
        this->resize_vector_variable (this->T);
    }

    void init()
    {
        this->Fflux.zero();
        this->THflux.zero();
        this->T.zero();
        if(doNoise)
        { //if noise is set to true, apply random values from 0 to noiseHeight
            this->noiseify_vector_variable (this->Fflux, 0.0, noiseHeight);
            this->noiseify_vector_variable (this->THflux, 0.0, noiseHeight);
        }

        init_coolant_rods();
        //init_control_rods();

    }

    void init_coolant_rods(){
        //HEX_USER_FLAG_0 IS A ***COOLANT*** channel.
        //How to set a flag at some cubic coordinate to have a flag with the findHexAt function.
        std::list<morph::Hex>::iterator pos; //set pos.

        pos = this->hg->findHexAt(morph::vec<int, 3>({0,0,0} ));
        if(pos != this->hg->hexen.end())
        { //if pos is the end hex, then it is probably out of range.
            pos->setUserFlags(HEX_USER_FLAG_0); //user flag 0 is coolant cell.}
        }
    }

    void init_control_rods()
    {
        //HEX_USER_FLAG_1 IS A ***CONTROL*** rod.
        //using flags on hexes to describe their properties
        //How to set a flag at some cubic coordinate to have a flag with the findHexAt function.
        std::list<morph::Hex>::iterator pos; //set pos.
        pos = this->hg->findHexAt(morph::vec<int, 3>({0,0,0}));
        if(pos != this->hg->hexen.end())
        { //if pos is the end hex, then it is probably out of range.
            pos->setUserFlags(HEX_USER_FLAG_1); //user flag 0 is coolant cell.}
        }
    }


    void compute_dFfluxdt (std::vector<Flt>& Fflux_, std::vector<Flt>& dFfluxdt)
    {
        std::vector<Flt> lapFflux(this->nhex, 0.0);
        this->compute_laplace (Fflux_, lapFflux);

        std::list<morph::Hex>::iterator hi = this->hg->hexen.begin(); //creates an iterator hi that starts on the first hex (hexen.begin)
        while(hi != this->hg->hexen.end()) // until the end of hi, run:
        {
            //calculating
            dFfluxdt[hi->vi] =0.005*lapFflux[hi->vi];

            //if the hex, hi, is a fuel cell.
            if(hi->getUserFlag(1)==true and Fflux[hi->vi]>=0.01)
            {
                Fflux[hi->vi] = Fflux[hi->vi] -0.01;
            }
            hi++; //iterate hi.
        }
    }

    void compute_dTHfluxdt (std::vector<Flt>& THflux_, std::vector<Flt>& dTHfluxdt)
    {
        std::vector<Flt> lapTHflux(this->nhex, 0.0);

        this->compute_laplace (THflux_, lapTHflux);

        std::list<morph::Hex>::iterator hi = this->hg->hexen.begin(); //creates an iterator hi that starts on the first hex (hexen.begin)
        while(hi != this->hg->hexen.end()) // until the end of hi, run:
        {
            //calculating
            dTHfluxdt[hi->vi] =0.005*lapTHflux[hi->vi];

            if(hi->getUserFlag(1)==true and Fflux[hi->vi]>=0.01)
            {
            THflux[hi->vi] += 0.01;
            }
            hi++; //iterate hi.
        }
    }


//compute rate of change of T with in input of a current state (T_) and a vector for the ROC (dTdt)
    void compute_dTdt(std::vector<Flt>& T_, std::vector<Flt>& dTdt)
    {
        //create a vector (lapT) for & compute the scalar laplacian over the hexgrid for T_.
        std::vector<Flt> lapT(this->nhex, 0.0);
        this->compute_laplace (T_, lapT);
        std::list<morph::Hex>::iterator hi = this->hg->hexen.begin(); //creates an iterator hi that starts on the first hex, (hexen.begin)
        while(hi != this->hg->hexen.end()) // until the end of hi, run:
        {
            //calculating dT/dt
            dTdt[hi->vi] =0.0005*THflux[hi->vi]+0.005*lapT[hi->vi]; //compute dT/dt

            //if the current hex is a coolant cell, negate a small amount of dT/dt
            //getUserFlag(0)=true if setUserFlags(HEX_USER_FLAG_0) has been run on
            //that iterator index, otherwise false. See init_coolant_rods().
            //and T at hi is greater than the subtraction I'm going to make.
            if(hi->getUserFlag(0)==true && T[hi->vi] >= 0.001)
            {
                dTdt[hi->vi] -= 0.001;
            }
            hi++; //iterate.
        }
    }


    void totalflux(){
        //use a parallelized for loop to calculate the total flux on the hex.
        #pragma omp parallel for
        for (unsigned int h=0; h<this->nhex; ++h) {
            total_flux[h] = Fflux[h]+THflux[h];
        }
    }
    void step(){
        stepFflux();
        stepTHflux();
        stepT();
        totalflux();
        this->stepCount++;
    }
    void stepFflux(){
        {
            // Ffluxtst: "Fflux at a test point". Ffluxtst is a temporary estimate for Fflux.
            std::vector<Flt> Ffluxtst(this->nhex, 0.0);
            std::vector<Flt> dFfluxdt(this->nhex, 0.0);
            std::vector<Flt> K1(this->nhex, 0.0);
            std::vector<Flt> K2(this->nhex, 0.0);
            std::vector<Flt> K3(this->nhex, 0.0);
            std::vector<Flt> K4(this->nhex, 0.0);
            /*
            * Stage 1
            */
            this->compute_dFfluxdt (this->Fflux, dFfluxdt);
            #pragma omp parallel for
            for (unsigned int h=0; h<this->nhex; ++h) {
                K1[h] = dFfluxdt[h] * this->dt;
                Ffluxtst[h] = this->Fflux[h] + K1[h] * 0.5 ;
            }

            /*
            * Stage 2
            */
            this->compute_dFfluxdt (Ffluxtst, dFfluxdt);
            #pragma omp parallel for
            for (unsigned int h=0; h<this->nhex; ++h) {
                K2[h] = dFfluxdt[h] * this->dt;
                Ffluxtst[h] = this->Fflux[h] + K2[h] * 0.5;
            }

            /*
            * Stage 3
            */
            this->compute_dFfluxdt (Ffluxtst, dFfluxdt);
            #pragma omp parallel for
            for (unsigned int h=0; h<this->nhex; ++h) {
                K3[h] = dFfluxdt[h] * this->dt;
                Ffluxtst[h] = this->Fflux[h] + K3[h];
            }

            /*
            * Stage 4
            */
            this->compute_dFfluxdt (Ffluxtst, dFfluxdt);
            #pragma omp parallel for
                for (unsigned int h=0; h<this->nhex; ++h) {
                K4[h] = dFfluxdt[h] * this->dt;
            }

            /*
            * Final sum. This could be incorporated in the for loop for stage 4.
            */
            #pragma omp parallel for
            for (unsigned int h=0; h<this->nhex; ++h) {
                Fflux[h] += ((K1[h] + 2.0 * (K2[h] + K3[h]) + K4[h])/(Flt)6.0);
            }
        }
    }
    void stepT(){
        {
            // Ttemp: "T at a test point". Ttemp is a temporary estimate for T.
            std::vector<Flt> Ttemp(this->nhex, 0.0);
            std::vector<Flt> dTdt(this->nhex, 0.0);
            std::vector<Flt> K1(this->nhex, 0.0);
            std::vector<Flt> K2(this->nhex, 0.0);
            std::vector<Flt> K3(this->nhex, 0.0);
            std::vector<Flt> K4(this->nhex, 0.0);
            /*
            * Stage 1
            */
            this->compute_dTdt (this->T, dTdt);
            #pragma omp parallel for
            for (unsigned int h=0; h<this->nhex; ++h) {
                K1[h] = dTdt[h] * this->dt;
                Ttemp[h] = this->T[h] + K1[h] * 0.5 ;
            }

            /*
            * Stage 2
            */
            this->compute_dTdt (Ttemp, dTdt);
            #pragma omp parallel for
            for (unsigned int h=0; h<this->nhex; ++h) {
                K2[h] = dTdt[h] * this->dt;
                Ttemp[h] = this->T[h] + K2[h] * 0.5;
            }

            /*
            * Stage 3
            */
            this->compute_dTdt (Ttemp, dTdt);
            #pragma omp parallel for
            for (unsigned int h=0; h<this->nhex; ++h) {
                K3[h] = dTdt[h] * this->dt;
                Ttemp[h] = this->T[h] + K3[h];
            }

            /*
            * Stage 4
            */
            this->compute_dTdt (Ttemp, dTdt);
            #pragma omp parallel for
                for (unsigned int h=0; h<this->nhex; ++h) {
                K4[h] = dTdt[h] * this->dt;
            }

            /*
            * Final sum. This could be incorporated in the for loop for stage 4.
            */
            #pragma omp parallel for
            for (unsigned int h=0; h<this->nhex; ++h) {
                T[h] += ((K1[h] + 2.0 * (K2[h] + K3[h]) + K4[h])/(Flt)6.0);
            }
        }
    }
    void stepTHflux(){
        {
            // THfluxtst: "THflux at a test point". THfluxtst is a temporary estimate for THflux.
            std::vector<Flt> THfluxtst(this->nhex, 0.0);
            std::vector<Flt> dTHfluxdt(this->nhex, 0.0);
            std::vector<Flt> K1(this->nhex, 0.0);
            std::vector<Flt> K2(this->nhex, 0.0);
            std::vector<Flt> K3(this->nhex, 0.0);
            std::vector<Flt> K4(this->nhex, 0.0);
            /*
            * Stage 1
            */
            this->compute_dTHfluxdt (this->THflux, dTHfluxdt);
            #pragma omp parallel for
            for (unsigned int h=0; h<this->nhex; ++h) {
                K1[h] = dTHfluxdt[h] * this->dt;
                THfluxtst[h] = this->THflux[h] + K1[h] * 0.5 ;
            }

            /*
            * Stage 2
            */
            this->compute_dTHfluxdt (THfluxtst, dTHfluxdt);
            #pragma omp parallel for
            for (unsigned int h=0; h<this->nhex; ++h) {
                K2[h] = dTHfluxdt[h] * this->dt;
                THfluxtst[h] = this->THflux[h] + K2[h] * 0.5;
            }

            /*
            * Stage 3
            */
            this->compute_dTHfluxdt (THfluxtst, dTHfluxdt);
            #pragma omp parallel for
            for (unsigned int h=0; h<this->nhex; ++h) {
                K3[h] = dTHfluxdt[h] * this->dt;
                THfluxtst[h] = this->THflux[h] + K3[h];
            }

            /*
            * Stage 4
            */
            this->compute_dTHfluxdt (THfluxtst, dTHfluxdt);
            #pragma omp parallel for
                for (unsigned int h=0; h<this->nhex; ++h) {
                K4[h] = dTHfluxdt[h] * this->dt;
            }

            /*
            * Final sum. This could be incorporated in the for loop for stage 4.
            */
            #pragma omp parallel for
            for (unsigned int h=0; h<this->nhex; ++h) {
                THflux[h] += ((K1[h] + 2.0 * (K2[h] + K3[h]) + K4[h])/(Flt)6.0);
            }
        }
    }
};


