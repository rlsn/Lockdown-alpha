#include "Art/LDTraversalTopology.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDTraversalTopologyTest,"Lockdown.Rules.StaticScene.TraversalTopology",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDTraversalTopologyTest::RunTest(const FString& Parameters)
{
    using namespace LDTraversalTopology;
    TArray<FNode> Nodes;
    for(int32 X=0;X<4;++X)
    {
        auto& Node=Nodes.AddDefaulted_GetRef();const double Left=X*200.,Right=(X+1)*200.;
        Node.Vertices={{Left,0,0},{Right,0,0},{Right,400,0},{Left,400,0}};
        if(X>0){Node.Neighbours.Add(X-1);}if(X<3){Node.Neighbours.Add(X+1);}
    }
    const FCells Open=ConvexCells(Nodes,{0,1,2,3});
    TestEqual(TEXT("A rectangular plaza split by tile edges has no invented street neck"),Open.Active.Num(),1);
    TestEqual(TEXT("Convex merging retains all walkable area"),Open.Cells[Open.Active[0]].Area,320000.);
    Nodes.SetNum(2);Nodes[0].Neighbours={1};Nodes[1].Neighbours={0};
    Nodes[0].Vertices={{0,0,0},{400,0,0},{400,200,0},{0,200,0}};
    Nodes[1].Vertices={{200,200,0},{400,200,0},{400,600,0},{200,600,0}};
    const FCells Corner=ConvexCells(Nodes,{0,1});
    TestEqual(TEXT("A concave street corner cannot be filled across its missing structure"),Corner.Active.Num(),2);
    const TArray<FPortal> SplitDoor={{{0,0,0},{0,100,0}},{{0,100,0},{0,300,0}},{{0,300,0},{0,600,0}}};
    const auto One=PortalGroups(SplitDoor);
    TestEqual(TEXT("Tile seams through one doorway do not invent multiple exits"),One.Num(),1);
    TestEqual(TEXT("Contiguous doorway width is retained"),PortalWidth(SplitDoor,One[0]),600.);
    const TArray<FPortal> TwoDoors={{{0,0,0},{0,200,0}},{{500,0,0},{500,200,0}}};
    TestEqual(TEXT("Two doors into the same plaza remain independent exits"),PortalGroups(TwoDoors).Num(),2);
    const FPortal BroadEntrance{{0,0,0},{0,1000,0}};
    const FVector ShallowCorner{100,990,0};
    TestEqual(TEXT("Depth near the end of a broad entrance excludes lateral entrance width"),
        FVector::Dist(ShallowCorner,ClosestEntrancePoint(ShallowCorner,BroadEntrance)),100.);
    const FVector DeepAlley{500,990,0};
    TestEqual(TEXT("The same broad entrance still identifies a genuine deep alley"),
        FVector::Dist(DeepAlley,ClosestEntrancePoint(DeepAlley,BroadEntrance)),500.);
    return !HasAnyErrors();
}
