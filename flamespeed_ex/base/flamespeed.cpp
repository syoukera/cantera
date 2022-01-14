/*!
 * @file flamespeed.cpp
 * C++ demo program to compute flame speeds using GRI-Mech.
 * Usage: flamespeed [equivalence_ratio] [refine_grid] [loglevel]
 */

#include "cantera/oneD/Sim1D.h"
#include "cantera/oneD/Boundary1D.h"
#include "cantera/oneD/StFlow.h"
#include "cantera/oneD/IonFlow.h"
#include "cantera/thermo/IdealGasPhase.h"
#include "cantera/transport.h"
#include <fstream>

using namespace Cantera;
using fmt::print;

int flamespeed(double phi, double eField, bool refine_grid, int loglevel)
{
    try {
        auto sol = newSolution("gri30_ion.yaml", "gas", "None");
        auto gas = sol->thermo();
        double temp = 300.0; // K
        double pressure = 1.0*OneAtm; //atm
        double uin = 0.3; //m/sec

        size_t nsp = gas->nSpecies();
        vector_fp x(nsp, 0.0);

        gas->setEquivalenceRatio(phi, "CH4", "O2:0.21,N2:0.79");
        gas->setState_TP(temp, pressure);
        gas->getMoleFractions(x.data());

        double rho_in = gas->density();

        vector_fp yin(nsp);
        gas->getMassFractions(&yin[0]);

        gas->equilibrate("HP");
        vector_fp yout(nsp);
        gas->getMassFractions(&yout[0]);
        double rho_out = gas->density();
        double Tad = gas->temperature();
        print("phi = {}, Tad = {}\n", phi, Tad);

        //=============  build each domain ========================


        //-------- step 1: create the flow -------------

        IonFlow flow(gas);
        // StFlow flow(gas);
        flow.setFreeFlow();

        // create an initial grid
        int nz = 6;
        double lz = 0.1;
        vector_fp z(nz);
        double dz = lz/((double)(nz-1));
        for (int iz = 0; iz < nz; iz++) {
            z[iz] = ((double)iz)*dz;
        }

        flow.setupGrid(nz, &z[0]);

        // specify the objects to use to compute kinetic rates and
        // transport properties

        std::unique_ptr<Transport> trmix(newTransportMgr("Ion", sol->thermo().get()));
        // std::unique_ptr<Transport> trmix(newTransportMgr("Mix", sol->thermo().get()));
        // std::unique_ptr<Transport> trmulti(newTransportMgr("Multi", sol->thermo().get()));

        flow.setTransport(*trmix);
        flow.setKinetics(*sol->kinetics());
        flow.setPressure(pressure);

        //------- step 2: create the inlet  -----------------------

        Inlet1D inlet;

        inlet.setMoleFractions(x.data());
        double mdot=uin*rho_in;
        inlet.setMdot(mdot);
        inlet.setTemperature(temp);


        //------- step 3: create the outlet  ---------------------

        Outlet1D outlet;

        //=================== create the container and insert the domains =====

        std::vector<Domain1D*> domains { &inlet, &flow, &outlet };
        Sim1D flame(domains);

        //----------- Supply initial guess----------------------

        vector_fp locs{0.0, 0.3, 0.7, 1.0};
        vector_fp value;

        double uout = inlet.mdot()/rho_out;
        value = {uin, uin, uout, uout};
        flame.setInitialGuess("velocity",locs,value);
        value = {temp, temp, Tad, Tad};
        flame.setInitialGuess("T",locs,value);

        for (size_t i=0; i<nsp; i++) {
            value = {yin[i], yin[i], yout[i], yout[i]};
            flame.setInitialGuess(gas->speciesName(i),locs,value);
        }

        inlet.setMoleFractions(x.data());
        inlet.setMdot(mdot);
        inlet.setTemperature(temp);

        flame.showSolution();

        int flowdomain = 1;
        double ratio = 10.0;
        double slope = 0.08;
        double curve = 0.1;

        flame.setRefineCriteria(flowdomain,ratio,slope,curve);

        // Solve freely propagating flame

        // Linearly interpolate to find location where this temperature would
        // exist. The temperature at this location will then be fixed for
        // remainder of calculation.
        flame.setFixedTemperature(0.5 * (temp + Tad));
        flow.solveEnergyEqn();

        flow.solveElectricField();

        flow.setSolvingStage(1);
        flame.solve(loglevel,refine_grid);

        vector_fp Evec, Vvec;
        double V_gap;

        // double eField = 1.0e0;
        inlet.setEField(eField);

        flow.setSolvingStage(2);
        try {
            flame.solve(loglevel,refine_grid);
            V_gap = flame.gapVoltage();
        } catch (CanteraError& err) {
            std::cerr << err.what() << std::endl;
            std::cerr << "program terminating." << std::endl;
            V_gap = std::numeric_limits<double>::quiet_NaN();
        }

        Evec.push_back(eField);
        Vvec.push_back(V_gap);
        std::cout << "Electric Field: " << eField << " Gap voltage: " << V_gap << std::endl;

        // for (size_t i; i!=Evec.size(); i++)
        // {   
        //     std::cout << Evec[i] << Vvec[i] << std::endl;
        // }

        std::string fname_csv_V = "gapvoltage_phi" + std::to_string(phi)  
                                    + "_eField" + std::to_string(eField) + ".csv";
        std::ofstream outfile(fname_csv_V, std::ios::trunc);
        outfile << "eField, gapVoltage\n";
        for (size_t n = 0; n != Evec.size(); n++) {
            print(outfile, " {:16.12e}, {:16.12e}\n", Evec[n], Vvec[n]);
        }
        outfile.close();

        
        vector_fp zvec,Tvec,Elevec,eFieldvec,Uvec;
        // print("\n{:9s}\t{:8s}\t{:5s}\t{:7s}\n",
        //     "z (m)", "T (K)", "Y(E)", "eField (V/m)");

        for (size_t n = 0; n < flow.nPoints(); n++) {
            Tvec.push_back(flame.workValue(flowdomain,flow.componentIndex("T"),n));
            Elevec.push_back(flame.workValue(flowdomain,
                                        flow.componentIndex("E"),n));
            eFieldvec.push_back(flame.workValue(flowdomain,
                                        flow.componentIndex("eField"),n));
            Uvec.push_back(flame.workValue(flowdomain,
                                    flow.componentIndex("velocity"),n));
            zvec.push_back(flow.grid(n));

            // print("{:9.6f}\t{:8.3e}\t{:8.3e}\t{:7.5f}\n",
            //     flow.grid(n), Tvec[n], Elevec[n], eFieldvec[n]);
        }
        
        // print("\nAdiabatic flame temperature from equilibrium is: {}\n", Tad);
        // print("Flame speed for phi={} is {} m/s.\n", phi, Uvec[0]);

        std::string fname_csv = "flamespeed_phi" + std::to_string(phi)  
                                    + "_eField" + std::to_string(eField) + ".csv";
        std::ofstream outfile2(fname_csv, std::ios::trunc);
        outfile2 << "  Grid,   Temperature,   Uvec,   E,    eField\n";
        for (size_t n = 0; n < flow.nPoints(); n++) {
            print(outfile2, " {:16.12e}, {:16.12e}, {:16.12e}, {:16.12e}, {:16.12e}\n",
                flow.grid(n), Tvec[n], Uvec[n], Elevec[n], eFieldvec[n]);
        }
        outfile2.close();

        std::string fname_solution = "flamespeed_phi" + std::to_string(phi)  
                                    + "_eField" + std::to_string(eField) + ".xml";
        flame.save(fname_solution, "sol", "Solutions", loglevel);
        // flame.saveResidual("flamespeed_res.xml", "res", "Resitudals", loglevel);

    } catch (CanteraError& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << "program terminating." << std::endl;
        return -1;
    }
    return 0;
}

int main(int argc, char** argv)
{
    double phi, eField;
    int loglevel = 1;
    bool refine_grid = true;
    if (argc >= 2) {
        phi = fpValue(argv[1]);
    } else {
        print("Enter phi: ");
        std::cin >> phi;
    }
    if (argc >= 3) {
        eField = fpValue(argv[2]);
    } else {
        print("Enter phi: ");
        std::cin >> eField;
    }
    if (argc >= 4) {
        refine_grid = bool(intValue(argv[3]));
    }
    if (argc >= 5) {
        loglevel = intValue(argv[4]);
    }
    return flamespeed(phi, eField, refine_grid, loglevel);
}