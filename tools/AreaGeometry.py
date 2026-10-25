"""Mod-owned Cursed Arena geometry. Metres, Y-up; physics winding."""
import math
import numpy as np
from shapely import Polygon, MultiPoint, box, delaunay_triangles, constrained_delaunay_triangles, orient_polygons
from shapely.ops import nearest_points
from shapely.affinity import scale
from shapely.geometry import Point

RADIUS = 24.0
PAD_RADIUS = 3.75
RAMP_HALF_WIDTH = 1.5
RAMP_END = 29.5
PAD_Z = RAMP_END + math.sqrt(PAD_RADIUS**2 - RAMP_HALF_WIDTH**2)
PAD_HEIGHT = 1.8
BONFIRE_LOCAL = np.array([0., PAD_HEIGHT, PAD_Z + .4])
ARRIVAL_LOCAL = np.array([0., PAD_HEIGHT + .05, PAD_Z - .8])
LAVA_Y = -3.2
DROP_DISTANCE = 28.5
RIM_HEIGHT = 1.3
BARRIER_HEIGHT = 8.0


def surface_height(z):
    return PAD_HEIGHT * min(1., max(0., (z - RADIUS) / (RAMP_END - RADIUS)))


# Two barely raised bumps, two moderate hills, and the existing hollows.
RELIEF = [(-10., -7., .006, 4.2), (9., 5., .007, 4.5),
          (-7., 12., .15, 3.6), (13., -11., .135, 3.5),
          (2., -10., -.22, 3.8), (-12., 5., -.18, 3.5),
          (4., 14., -.16, 3.2)]
GRASS_PATCHES = [(-15., -9.), (13., 11.), (-10., 15.),
                 (16., -8.), (-16., 5.), (3., -17.), (7., 17.), (-9., -16.),
                 (-7.,-5.), (6.,-7.), (-2.,10.), (14.,2.), (-18.,-1.),
                 (-3.,-19.), (-14.,11.), (1.,17.)]
# Two central rocks and three outer rocks, with open routes between them.
COVER_ROCKS = [(-4.,-2.,1.05,.85,1.85), (5.,3.,.95,1.1,2.),
               (-15.,-6.,1.2,.95,1.9), (12.,-13.,.95,1.15,1.8),
               (8.,16.,1.15,.9,2.05)]


def cover_footprints():
    return [scale(Point(x,z).buffer(1.,quad_segs=8),xfact=sx*1.12,yfact=sz*1.12,origin=(x,z)).buffer(.2)
            for x,z,sx,sz,h in COVER_ROCKS]


def cover_rock(index,vertices,faces):
    cx,cz,sx,sz,height=COVER_ROCKS[index]
    n=12
    ring=[(cx+sx*math.cos(i*math.tau/n),cz+sz*math.sin(i*math.tau/n)) for i in range(n)]
    bottom=min(sampled_height(x,z,vertices,faces) for x,z in ring)-.2
    top=sampled_height(cx,cz,vertices,faces)+height
    mesh=Mesh()
    centre=mesh.vertex(cx,bottom,cz)
    rings=[]
    for radius,fraction in [(.88,0.),(1.,.3),(.72,.8)]:
        rings.append([mesh.vertex(cx+(x-cx)*radius,bottom+(top-bottom)*fraction,cz+(z-cz)*radius) for x,z in ring])
    peak=mesh.vertex(cx+sx*.12,top,cz-sz*.08)
    for i in range(n):
        j=(i+1)%n
        mesh.face(centre,rings[0][i],rings[0][j])
        for lo,hi in zip(rings,rings[1:]):
            mesh.face(lo[i],hi[j],lo[j]);mesh.face(lo[i],hi[i],hi[j])
        mesh.face(peak,rings[-1][j],rings[-1][i])
    return mesh.arrays()


def terrain_height(x,z):
    fade = min(1., max(0., (RADIUS - math.hypot(x,z)) / 3.))
    fade = fade * fade * (3. - 2. * fade)
    y = sum(h * math.exp(-((x-cx)**2+(z-cz)**2)/(w*w)) for cx,cz,h,w in RELIEF)
    # Keep the former large hills genuinely subtle, including their fine ripple.
    quiet=max(math.exp(-((x-cx)**2+(z-cz)**2)/(w*w)) for cx,cz,h,w in RELIEF[:2])
    y += .025 * (1.-.9*quiet) * math.sin(x*.7+z*.31) * math.sin(z*.61-x*.23)
    y *= fade * 10.
    return y * .5


def ground_patch(x,z):
    """Seeded, fine-grained stone mottling rather than broad circular islands."""
    def noise(x,z):
        ix,iz=math.floor(x),math.floor(z)
        u,v=x-ix,z-iz
        u=u*u*(3.-2.*u);v=v*v*(3.-2.*v)
        def value(a,b):return (math.sin(a*127.1+b*311.7+99.14)*43758.5453)%1.
        return ((1.-u)*value(ix,iz)+u*value(ix+1,iz))*(1.-v)+((1.-u)*value(ix,iz+1)+u*value(ix+1,iz+1))*v
    score=.6*noise(x/1.6,z/1.6)+.4*noise(x/.55,z/.55)
    return math.hypot(x,z)<22.8 and score>.6442


def sampled_height(x,z,vertices,faces):
    tri=vertices[faces].astype(np.float64)
    a=tri[:,1][:,[0,2]]-tri[:,0][:,[0,2]]
    b=tri[:,2][:,[0,2]]-tri[:,0][:,[0,2]]
    q=np.array([x,z])-tri[:,0][:,[0,2]]
    det=a[:,0]*b[:,1]-a[:,1]*b[:,0]
    u=(q[:,0]*b[:,1]-q[:,1]*b[:,0])/det
    v=(a[:,0]*q[:,1]-a[:,1]*q[:,0])/det
    hit=np.flatnonzero((u>=-1e-6)&(v>=-1e-6)&(u+v<=1.000001))
    assert len(hit), (x,z)
    i=hit[0]
    return float(tri[i,0,1]+u[i]*(tri[i,1,1]-tri[i,0,1])+v[i]*(tri[i,2,1]-tri[i,0,1]))


GRASS_BLADE_COUNT = 22536  # Three times the previous 7,512 blades.


SHORT_GRASS_DENSITY = 36.  # Blades per square metre in the newly covered dirt.
SHORT_GRASS_TILE = 1.4
BORDER_GRASS_SECTORS = 96
BORDER_GRASS_RUNS = [(3,8),(32,8),(54,8),(78,8)]
BORDER_GRASS_BLADES_PER_SECTOR = 180


def border_grass_region():
    """Four continuous border stretches, totalling one third of the circle."""
    from shapely import union_all
    patches=[]
    for start,count in BORDER_GRASS_RUNS:
        angles=np.linspace(start*math.tau/BORDER_GRASS_SECTORS,(start+count)*math.tau/BORDER_GRASS_SECTORS,count*6+1)
        outer=[(23.75*math.cos(t),23.75*math.sin(t)) for t in angles]
        inner=[(22.*math.cos(t),22.*math.sin(t)) for t in angles[::-1]]
        patches.append(Polygon(outer+inner))
    return union_all(patches)


def short_grass_regions(ground,occupied):
    """Choose half of the bare dirt by area, in scattered deterministic tiles."""
    bare=ground.difference(occupied)
    cells=[]
    left,bottom,right,top=bare.bounds
    for ix in range(math.floor(left/SHORT_GRASS_TILE),math.ceil(right/SHORT_GRASS_TILE)):
        for iz in range(math.floor(bottom/SHORT_GRASS_TILE),math.ceil(top/SHORT_GRASS_TILE)):
            cell=bare.intersection(box(ix*SHORT_GRASS_TILE,iz*SHORT_GRASS_TILE,(ix+1)*SHORT_GRASS_TILE,(iz+1)*SHORT_GRASS_TILE))
            if cell.area>.1 and cell.buffer(-.06).area>.01:cells.append(cell)
    rng=np.random.default_rng(14109901)
    rng.shuffle(cells)
    selected=[];area=0.;target=bare.area*.5
    for cell in cells:
        if abs(area+cell.area-target)<abs(area-target):
            selected.append(cell);area+=cell.area
    assert abs(area-target)<2., 'Short grass must cover half of the bare dirt.'
    return bare,selected


def grass(vertices,faces,ground_faces):
    """Grow mixed-height clumps only within the actual rendered ground faces."""
    from shapely import union_all, prepare
    ground=union_all([Polygon(vertices[face][:,[0,2]]) for face in ground_faces])
    # Require the entire blade projection (including its lean) to stay on dirt.
    growing=ground.difference(union_all(cover_footprints())).buffer(-.015)
    prepare(growing)
    rng=np.random.default_rng(14109900)
    mesh=Mesh();occupied=[];interior_tall=set()
    for index,(cx,cz) in enumerate(GRASS_PATCHES):
        target=GRASS_BLADE_COUNT//len(GRASS_PATCHES)+(index<GRASS_BLADE_COUNT%len(GRASS_PATCHES))
        start=len(mesh.faces);start_vertex=len(mesh.vertices);attempts=0
        while len(mesh.faces)-start<target:
            attempts+=1
            assert attempts<target*30, 'Insufficient ground for the requested grass density.'
            angle=rng.uniform(0,math.tau);radius=math.sqrt(rng.random())
            x=cx+2.7*radius*math.cos(angle);z=cz+1.65*radius*math.sin(angle)
            for _ in range(min(3,target-(len(mesh.faces)-start))):
                px=x+rng.uniform(-.14,.14);pz=z+rng.uniform(-.14,.14)
                t=rng.uniform(0,math.tau);w=rng.uniform(.025,.055)
                is_tall=rng.random()<.7
                h=rng.uniform(.7,1.6) if is_tall else rng.uniform(.22,.55)
                h*=1.-.25*radius
                dx,dz=math.cos(t)*w,math.sin(t)*w
                lean=rng.uniform(.06,.22)
                tip_x=px+math.sin(t)*lean;tip_z=pz-math.cos(t)*lean
                footprint=Polygon([(px-dx,pz-dz),(px+dx,pz+dz),(tip_x,tip_z)])
                if not growing.covers(footprint):continue
                y=sampled_height(px,pz,vertices,faces)
                a=mesh.vertex(px-dx,sampled_height(px-dx,pz-dz,vertices,faces)-.012,pz-dz)
                b=mesh.vertex(px+dx,sampled_height(px+dx,pz+dz,vertices,faces)-.012,pz+dz)
                c=mesh.vertex(tip_x,y+h,tip_z)
                if is_tall:interior_tall.add(len(mesh.faces))
                mesh.face(a,b,c)
        occupied.append(MultiPoint(np.asarray(mesh.vertices[start_vertex:])[:,[0,2]]).convex_hull.buffer(.12))
    bare,short_cells=short_grass_regions(growing,union_all(occupied))
    prepare(bare)
    rng=np.random.default_rng(14109902)
    for cell in short_cells:
        target=max(1,round(cell.area*SHORT_GRASS_DENSITY));count=0;attempts=0
        left,bottom,right,top=cell.bounds
        while count<target:
            attempts+=1
            assert attempts<max(300,target*100), 'Insufficient bare dirt inside a short-grass tile.'
            px=rng.uniform(left,right);pz=rng.uniform(bottom,top)
            if not cell.covers(Point(px,pz)):continue
            t=rng.uniform(0,math.tau);w=rng.uniform(.018,.035)
            dx,dz=math.cos(t)*w,math.sin(t)*w
            lean=rng.uniform(.02,.06)
            tx=px+math.sin(t)*lean;tz=pz-math.cos(t)*lean
            footprint=Polygon([(px-dx,pz-dz),(px+dx,pz+dz),(tx,tz)])
            if not bare.covers(footprint):continue
            y=sampled_height(px,pz,vertices,faces)
            a=mesh.vertex(px-dx,sampled_height(px-dx,pz-dz,vertices,faces)-.012,pz-dz)
            b=mesh.vertex(px+dx,sampled_height(px+dx,pz+dz,vertices,faces)-.012,pz+dz)
            c=mesh.vertex(tx,y+rng.uniform(.1,.22),tz)
            mesh.face(a,b,c);count+=1
    # Retain the existing short blades and their coverage exactly. The old tall
    # clumps only define the historic bare-dirt mask above; they are not rendered.
    mesh.faces=[face for index,face in enumerate(mesh.faces) if index not in interior_tall]
    short_count=len(mesh.faces)
    border=growing.intersection(border_grass_region())
    prepare(border)
    rng=np.random.default_rng(14109903)
    for start,length in BORDER_GRASS_RUNS:
        for sector in range(start,start+length):
            count=0;attempts=0
            while count<BORDER_GRASS_BLADES_PER_SECTOR:
                attempts+=1
                assert attempts<BORDER_GRASS_BLADES_PER_SECTOR*100, 'Insufficient dirt in a border grass sector.'
                angle=rng.uniform(sector,sector+1)*math.tau/BORDER_GRASS_SECTORS
                radius=math.sqrt(rng.uniform(22.05**2,23.7**2))
                px,pz=radius*math.cos(angle),radius*math.sin(angle)
                t=rng.uniform(0,math.tau);w=rng.uniform(.025,.055)
                dx,dz=math.cos(t)*w,math.sin(t)*w
                lean=rng.uniform(.06,.2)
                tx=px+math.sin(t)*lean;tz=pz-math.cos(t)*lean
                footprint=Polygon([(px-dx,pz-dz),(px+dx,pz+dz),(tx,tz)])
                if not border.covers(footprint):continue
                y=sampled_height(px,pz,vertices,faces)
                a=mesh.vertex(px-dx,sampled_height(px-dx,pz-dz,vertices,faces)-.012,pz-dz)
                b=mesh.vertex(px+dx,sampled_height(px+dx,pz+dz,vertices,faces)-.012,pz+dz)
                c=mesh.vertex(tx,y+rng.uniform(.75,1.5),tz)
                mesh.face(a,b,c);count+=1
    v,f=mesh.arrays()
    return v,f,dict(bare_area=bare.area,short_grass_area=sum(cell.area for cell in short_cells),
                   short_blades=short_count,border_tall_blades=len(f)-short_count,
                   border_fraction=sum(count for start,count in BORDER_GRASS_RUNS)/BORDER_GRASS_SECTORS)


def floor_polygons():
    join_z=math.sqrt(RADIUS**2-RAMP_HALF_WIDTH**2)
    join_angle=math.atan2(join_z,RAMP_HALF_WIDTH)
    arc=[(RADIUS*math.cos(a),RADIUS*math.sin(a)) for a in np.linspace(join_angle, -math.pi-join_angle, 129)]
    # Exact shared 3 m chords keep the rectangle attached to both circles.
    arc[0]=(RAMP_HALF_WIDTH,join_z);arc[-1]=(-RAMP_HALF_WIDTH,join_z)
    main=Polygon(arc+[(-RAMP_HALF_WIDTH,RADIUS),(RAMP_HALF_WIDTH,RADIUS)])
    ramp=Polygon([(-RAMP_HALF_WIDTH,RADIUS),(-RAMP_HALF_WIDTH,RAMP_END),(RAMP_HALF_WIDTH,RAMP_END),(RAMP_HALF_WIDTH,RADIUS)])
    a=math.atan2(RAMP_END-PAD_Z,-RAMP_HALF_WIDTH)
    b=math.atan2(RAMP_END-PAD_Z,RAMP_HALF_WIDTH)-math.tau
    cap=[(PAD_RADIUS*math.cos(t),PAD_Z+PAD_RADIUS*math.sin(t)) for t in np.linspace(a,b,97)]
    cap[0]=(-RAMP_HALF_WIDTH,RAMP_END);cap[-1]=(RAMP_HALF_WIDTH,RAMP_END)
    return main,ramp,Polygon(cap)


def outline():
    main,ramp,pad=floor_polygons()
    result=main.union(ramp).union(pad)
    assert result.geom_type=='Polygon' and result.is_valid and not result.interiors
    return result


def contour(poly):
    return np.asarray(orient_polygons(poly,exterior_cw=True).exterior.coords[:-1],dtype=np.float64)


class Mesh:
    def __init__(self):self.vertices=[];self.faces=[];self.lookup={}
    def vertex(self,x,y,z):
        key=tuple(round(float(a),6) for a in (x,y,z))
        if key not in self.lookup:self.lookup[key]=len(self.vertices);self.vertices.append([x,y,z])
        return self.lookup[key]
    def face(self,a,b,c):
        if len({a,b,c})==3:self.faces.append([a,b,c])
    def plane(self,poly,height,up=True):
        for tri in constrained_delaunay_triangles(poly).geoms:
            coords=contour(tri)
            ids=[self.vertex(x,height(x,z),z) for x,z in coords]
            self.face(*(ids if up else ids[::-1]))
    def wall(self,poly,lower,upper,inward=False):
        ring=contour(poly)
        lo=[self.vertex(x,lower(x,z),z) for x,z in ring]
        hi=[self.vertex(x,upper(x,z),z) for x,z in ring]
        for i in range(len(ring)):
            j=(i+1)%len(ring)
            fs=[(lo[i],hi[j],hi[i]),(lo[i],lo[j],hi[j])]
            for f in fs:self.face(*(f[::-1] if inward else f))
    def arrays(self):return np.asarray(self.vertices,dtype=np.float32),np.asarray(self.faces,dtype=np.uint32)


def island():
    shape=outline();mesh=Mesh()
    main,ramp,pad=floor_polygons()
    # Triangulate a jittered interior lattice while retaining the original rim
    # vertices. The tiny straight neck remains a separate, level rectangle.
    join_z=math.sqrt(RADIUS**2-RAMP_HALF_WIDTH**2)
    circle=main.intersection(box(-RADIUS-1,-RADIUS-1,RADIUS+1,join_z))
    points=list(contour(circle))
    rng=np.random.default_rng(9900)
    for z in np.arange(-22.5,23.,1.25):
        for x in np.arange(-22.5,23.,1.25):
            px,pz=x+rng.uniform(-.2,.2),z+rng.uniform(-.2,.2)
            if circle.contains(Point(px,pz)):points.append((px,pz))
    tiles=delaunay_triangles(MultiPoint(points))
    assert abs(sum(t.area for t in tiles.geoms)-circle.area)<1e-6
    for tri in tiles.geoms:
        coords=contour(tri)
        mesh.face(*[mesh.vertex(x,terrain_height(x,z),z) for x,z in coords])
    mesh.plane(main.difference(circle),lambda x,z:0.)
    mesh.plane(ramp,lambda x,z:surface_height(z))
    mesh.plane(pad,lambda x,z:PAD_HEIGHT)
    walk_faces=np.asarray(mesh.faces,dtype=np.uint32)
    # Derive the whole rim from the real concave outline, including both
    # junctions. Buffering avoids gaps or crossing strips beside the ramp.
    boundary=shape.boundary
    def edge_height(x,z):return surface_height(nearest_points(boundary,Point(x,z))[0].y)
    previous=shape;previous_y=lambda x,z:edge_height(x,z)
    for offset,height in [(.15,RIM_HEIGHT),(.7,RIM_HEIGHT),(1.4,1.1),(3.8,-3.8)]:
        outer=shape.buffer(offset,quad_segs=4)
        region=outer.difference(previous)
        # Every annular triangle has vertices on its two boundary loops.
        inner_line=previous.boundary;outer_line=outer.boundary
        def y(x,z,inner=inner_line,out=outer_line,old=previous_y,h=height):
            p=Point(x,z)
            if inner.distance(p)<out.distance(p):return old(x,z)
            return h if h<0 else edge_height(x,z)+h
        mesh.plane(region,y)
        previous=outer
        previous_y=(lambda x,z,h=height: h if h<0 else edge_height(x,z)+h)
    mesh.wall(previous,lambda x,z:-5.5,lambda x,z:-3.8)
    mesh.plane(previous,lambda x,z:-5.5,up=False)
    v,f=mesh.arrays()
    return v,f,walk_faces,shape


def barrier(shape):
    """A closed collision-only ring behind the visible rim, well above a jump."""
    inner=shape.buffer(.48,quad_segs=4);outer=shape.buffer(.65,quad_segs=4)
    boundary=shape.boundary
    def base(x,z):return surface_height(nearest_points(boundary,Point(x,z))[0].y)
    lo=lambda x,z:base(x,z)-1.
    hi=lambda x,z:base(x,z)+BARRIER_HEIGHT
    mesh=Mesh()
    mesh.wall(inner,lo,hi,inward=True);mesh.wall(outer,lo,hi)
    shell=outer.difference(inner)
    mesh.plane(shell,hi);mesh.plane(shell,lo,up=False)
    return mesh.arrays()


def lava(shape,falling=False):
    mesh=Mesh()
    if not falling:
        inner=shape.buffer(3.,quad_segs=4)
        for offset in [6.,9.,12.,16.,20.,24.,DROP_DISTANCE]:
            outer=shape.buffer(offset,quad_segs=4)
            mesh.plane(outer.difference(inner),lambda x,z:LAVA_Y)
            inner=outer
    else:
        # Matching edge coordinates close the seam; the fall fades into void.
        ring=contour(shape.buffer(DROP_DISTANCE,quad_segs=4))
        old=None
        for scale,y in [(1.,LAVA_Y),(1.001,-8.),(1.003,-15.),(1.005,-28.)]:
            ids=[mesh.vertex(x*scale,y,z*scale) for x,z in ring]
            if old is not None:
                for i in range(len(ids)):
                    j=(i+1)%len(ids)
                    mesh.face(old[i],ids[i],ids[j]);mesh.face(old[i],ids[j],old[j])
            old=ids
    return mesh.arrays()


def boulder(index):
    """Closed basalt boulders using existing rock materials."""
    n=10;points=[];faces=[]
    angle=index*math.tau/10+.12*math.sin(index*1.7)
    radius=RADIUS+6.6+(index%3)*.45
    # Keep the northern boulders clear of the larger bonfire platform.
    centre=np.array([radius*math.cos(angle),-3.9,radius*math.sin(angle)])
    if centre[2]>24 and abs(centre[0])<8:centre[0]+=9
    sx=.65+(index%3)*.16;sz=.7+(index%4)*.12;height=2.+(index%4)*.55
    points.append([0.,0.,0.])
    for ring,(r,y) in enumerate([(.7,0.),(1.,.35),(.65,.8)]):
        for i in range(n):
            t=i*math.tau/n;rough=1+.13*math.sin(i*2.3+index+ring*.5)
            points.append([sx*r*rough*math.cos(t),height*y,sz*r*rough*math.sin(t)])
    top=len(points);points.append([sx*.1,height,sz*.05])
    for i in range(n):
        j=(i+1)%n;faces.append([0,1+i,1+j])
        for ring in range(2):
            a=1+ring*n+i;b=1+ring*n+j;c=a+n;d=b+n
            faces.extend([[a,b,d],[a,d,c]])
        faces.append([top,1+2*n+j,1+2*n+i])
    return np.array(points,dtype=np.float32)+centre,np.array(faces,dtype=np.uint32)
