#include "VamBreastDebugTrajectory.h"
void FVamMotionRamp::Start(double V,double A,double J,double End,double Seconds)
{
    Duration=FMath::Max(.001,Seconds);Target=End;
    Coefficients[0]=V;Coefficients[1]=A*Duration;Coefficients[2]=.5*J*Duration*Duration;
    const double D=End-Coefficients[0]-Coefficients[1]-Coefficients[2],DA=-Coefficients[1]-2*Coefficients[2],DJ=-2*Coefficients[2];
    Coefficients[3]=10*D-4*DA+.5*DJ;Coefficients[4]=-15*D+7*DA-DJ;Coefficients[5]=6*D-3*DA+.5*DJ;
}
void FVamMotionRamp::Evaluate(double Time,double& X,double& V,double& A,double& J) const
{
    const double U=FMath::Clamp(Time/Duration,0.,1.);X=V=A=J=0;
    for(int32 I=0;I<6;++I)
    {
        X+=Duration*Coefficients[I]*FMath::Pow(U,I+1)/(I+1);V+=Coefficients[I]*FMath::Pow(U,I);
        if(I>=1) A+=I*Coefficients[I]*FMath::Pow(U,I-1)/Duration;
        if(I>=2) J+=I*(I-1)*Coefficients[I]*FMath::Pow(U,I-2)/(Duration*Duration);
    }
    if(Time>=Duration) { X+=Target*(Time-Duration);V=Target;A=J=0; }
}
namespace
{
void TakeoffSample(const FVamJumpTrajectory& R,double T,double& X,double& V,double& A,double& J)
{
    const double U=T/R.Takeoff,B=2*R.LaunchSpeed/R.Takeoff+R.Gravity;
    const double S=10*FMath::Pow(U,3)-15*FMath::Pow(U,4)+6*FMath::Pow(U,5);
    A=B*FMath::Square(FMath::Sin(UE_DOUBLE_PI*U))-R.Gravity*S;
    J=B*UE_DOUBLE_PI*FMath::Sin(2*UE_DOUBLE_PI*U)/R.Takeoff-R.Gravity*30*U*U*FMath::Square(1-U)/R.Takeoff;
    V=B*(T*.5-R.Takeoff*FMath::Sin(2*UE_DOUBLE_PI*U)/(4*UE_DOUBLE_PI))-R.Gravity*R.Takeoff*(2.5*FMath::Pow(U,4)-3*FMath::Pow(U,5)+FMath::Pow(U,6));
    X=B*(T*T*.25+R.Takeoff*R.Takeoff*(FMath::Cos(2*UE_DOUBLE_PI*U)-1)/(8*UE_DOUBLE_PI*UE_DOUBLE_PI))-R.Gravity*R.Takeoff*R.Takeoff*(.5*FMath::Pow(U,5)-.5*FMath::Pow(U,6)+FMath::Pow(U,7)/7);
}
}
double FVamJumpTrajectory::FlightTime() const
{
    double X,V,A,J;TakeoffSample(*this,Takeoff,X,V,A,J);
    const double B=V-.5*Gravity*Landing,C=X+.5*V*Landing-Gravity*Landing*Landing/10;
    return (B+FMath::Sqrt(B*B+2*Gravity*C))/Gravity;
}
void FVamJumpTrajectory::Evaluate(double T,double& X,double& V,double& A,double& J) const
{
    T=FMath::Max(0.,T);if(T<Takeoff) { TakeoffSample(*this,T,X,V,A,J);return; }
    double X0,V0,A0,J0;TakeoffSample(*this,Takeoff,X0,V0,A0,J0);
    const double Flight=FlightTime(),Air=FMath::Min(T-Takeoff,Flight);
    X0+=V0*Air-.5*Gravity*Air*Air;V0-=Gravity*Air;
    if(T<Takeoff+Flight) { X=X0;V=V0;A=-Gravity;J=0;return; }
    FVamMotionRamp Contact;Contact.Start(V0,-Gravity,0,0,Landing);
    Contact.Evaluate(T-Takeoff-Flight,X,V,A,J);X+=X0;
}
